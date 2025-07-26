// voltronic-mqtt-rpi v2.1.2 (GitHub Release)
// Authors: Luca De Giovanni & Google AI Studio
// Date: July 26, 2024
// Description: A lightweight C monitoring tool for Voltronic (Axpert/MPP Solar)
//              inverters, publishing data to MQTT. Designed for Raspberry Pi
//              and Venus OS compatibility. This version implements a stable
//              handshake and polling loop, reading data from QPIGS, QPIRI, etc.

// --- Standard library includes ---
#include <stdio.h>      // For printf, fprintf, sprintf, etc.
#include <stdlib.h>     // For atoi, exit, etc.
#include <string.h>     // For string manipulation functions
#include <unistd.h>     // For sleep and POSIX functions
#include <hidapi/hidapi.h> // For USB HID communication with inverter
#include <MQTTClient.h> // For MQTT communication
#include <time.h>       // For time measurement (timeouts)

// --- Constants and macros ---
#define MAX_BUFFER_SIZE 512           // Maximum buffer size for responses
#define INVERTER_VID 0x0665           // USB Vendor ID for Voltronic inverter
#define INVERTER_PID 0x5161           // USB Product ID for Voltronic inverter

// --- DATA STRUCTURES ---
// Structure to hold data from QPIGS command
// Each field corresponds to a value returned by the inverter
// (see Voltronic protocol documentation for details)
typedef struct { /* Data from QPIGS */
    float grid_voltage, grid_frequency, output_voltage, output_frequency;
    int output_apparent_power, output_active_power, output_load_percent, bus_voltage;
    float battery_voltage; int battery_charge_current, battery_capacity_percent, inverter_heat_sink_temp;
    float pv1_input_current, pv1_input_voltage;
    float battery_voltage_scc; int battery_discharge_current;
    char device_status[9];
    int battery_voltage_offset, eeprom_version, pv1_power;
    char device_status_2[4];
} PIGSData;

// Structure to hold data from QPIGS2 command
// (for inverters with dual PV input)
typedef struct { /* Data from QPIGS2 */
    float pv2_input_current, pv2_input_voltage;
    int   pv2_power;
} PIGS2Data;

// Structure to hold data from QPIRI command (inverter configuration)
typedef struct { /* Data from QPIRI */
    float grid_rating_voltage, grid_rating_current, ac_output_rating_voltage, ac_output_rating_frequency, ac_output_rating_current;
    int ac_output_rating_apparent_power, ac_output_rating_active_power;
    float battery_rating_voltage, battery_recharge_voltage, battery_under_voltage, battery_bulk_voltage, battery_float_voltage;
    int battery_type, max_ac_charging_current, max_charging_current;
} PIRIData;

// --- HELPER FUNCTIONS ---
// Calculate CRC16 for Voltronic protocol (used for command integrity)
unsigned short crc16(const unsigned char *buf, size_t len) {
    unsigned short crc = 0;
    while (len--) {
        crc ^= *buf++ << 8;
        for (int i = 0; i < 8; i++)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

// Publish a single metric to MQTT
// client: MQTT client handle
// base_topic: root topic (e.g. "inverter")
// sub_topic: subtopic (e.g. "pv1")
// metric_name: name of the metric (e.g. "pv1_input_voltage")
// value_str: value as string
void publish_metric(MQTTClient client, const char* base_topic, const char* sub_topic, const char* metric_name, const char* value_str) {
    if (value_str == NULL || strlen(value_str) == 0) return; // Skip empty values
    char full_topic[256];
    sprintf(full_topic, "%s/%s/%s", base_topic, sub_topic, metric_name); // Compose full topic
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void*)value_str; pubmsg.payloadlen = strlen(value_str);
    pubmsg.qos = 1; pubmsg.retained = 1;
    MQTTClient_publishMessage(client, full_topic, &pubmsg, NULL); // Publish to MQTT
}

// Flush any unread data from the HID buffer (to avoid stale data)
void flush_hid_buffer(hid_device *handle) {
    unsigned char dummy_buffer[64];
    while(hid_read_timeout(handle, dummy_buffer, sizeof(dummy_buffer), 50) > 0);
}

// Send a command to the inverter and read the response
// Returns number of bytes read, or -1 on error
int query_inverter(hid_device *handle, const char *command, char *response) {
    flush_hid_buffer(handle); // Clean buffer before sending
    unsigned char buffer_to_send[MAX_BUFFER_SIZE];
    unsigned short crc = crc16((unsigned char*)command, strlen(command)); // Calculate CRC
    int len = sprintf((char*)buffer_to_send, "%s%c%c\r", command, (crc & 0xff), (crc >> 8)); // Compose command with CRC
    if (hid_write(handle, buffer_to_send, len) < 0) {
        fprintf(stderr, "hid_write error for command %s\n", command);
        return -1;
    }
    int total_bytes = 0;
    time_t start_time = time(NULL);
    // Read response with timeout (max 3 seconds)
    while (difftime(time(NULL), start_time) < 3.0) {
        int rc = hid_read_timeout(handle, (unsigned char*)response + total_bytes, MAX_BUFFER_SIZE - total_bytes - 1, 200);
        if (rc > 0) {
            total_bytes += rc;
            // Look for end of response (carriage return '\r')
            if (memchr(response, '\r', total_bytes) != NULL) {
                char *end_ptr = memchr(response, '\r', total_bytes);
                if (end_ptr && (end_ptr - response) > 2) { *(end_ptr - 2) = '\0'; } else { response[0] = '\0'; }
                return total_bytes;
            }
        } else if (rc < 0) { fprintf(stderr, "hid_read error for command %s\n", command); return -1; }
    }
    response[0] = '\0'; // Timeout: empty response
    return 0;
}

// --- MAIN PROGRAM ENTRY POINT ---
int main(int argc, char* argv[]) {
    // Check command line arguments
    if (argc < 6) { fprintf(stderr, "Usage: %s <host> <user> <pass> <base_topic> <interval>\n", argv[0]); return 1; }
    char* mqtt_host = argv[1], *mqtt_user = argv[2], *mqtt_pass = argv[3], *mqtt_base_topic = argv[4];
    int interval_seconds = atoi(argv[5]); // Convert interval to integer

    // Initialize HIDAPI library
    hid_init();
    // Open HID device (the inverter)
    hid_device *handle = hid_open(INVERTER_VID, INVERTER_PID, NULL);
    if (!handle) { fprintf(stderr, "Unable to open HID device.\n"); return 1; }
    printf("HID device opened successfully.\n");
    
    printf("Stabilization pause (2 seconds)...\n");
    sleep(2); // Wait for device to stabilize

    // --- MQTT SETUP ---
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    char mqtt_address[100];
    sprintf(mqtt_address, "tcp://%s:1883", mqtt_host); // Compose MQTT broker address
    MQTTClient_create(&client, mqtt_address, "voltronic-rpi-client", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20; conn_opts.cleansession = 1;
    conn_opts.username = mqtt_user; conn_opts.password = mqtt_pass;
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) { fprintf(stderr, "Failed to connect to MQTT broker.\n"); return 1; }
    printf("Connected to MQTT broker: %s\n", mqtt_host);
    
    char response[MAX_BUFFER_SIZE]; // Buffer for inverter responses
    char value_buffer[50];          // Buffer for stringified values

    // --- INITIAL HANDSHAKE ---
    printf("\n--- Starting Initial Handshake ---\n");
    if (query_inverter(handle, "QPI", response) > 0) { printf("QPI response: %s\n", response); } else { printf("No response from QPI.\n"); }
    if (query_inverter(handle, "QVFW", response) > 0) { printf("QVFW response: %s\n", response); } else { printf("No response from QVFW.\n"); }
    printf("--- Handshake Finished ---\n\n");
    
    // --- MAIN POLLING LOOP ---
    while (1) {
        printf("Starting reading cycle...\n");
        PIRIData piri_data = {0};   // Struct for QPIRI data
        PIGS2Data pigs2_data = {0}; // Struct for QPIGS2 data
        char qmod_char = ' ';       // Inverter mode character
        char qid_response[32] = "N/A"; // Serial number
        PIGSData pigs_data = {0};   // Struct for QPIGS data
        
        // --- STEP 1: DATA GATHERING ---
        // Query inverter for configuration and status
        if (query_inverter(handle, "QPIRI", response) > 0) { 
            printf("QPIRI response: %s\n", response); 
            // Parse QPIRI response into struct fields
            sscanf(response + 1, "%f %f %f %f %f %d %d %f %f %f %f %f %d %d %d", 
                &piri_data.grid_rating_voltage, &piri_data.grid_rating_current, &piri_data.ac_output_rating_voltage, 
                &piri_data.ac_output_rating_frequency, &piri_data.ac_output_rating_current, &piri_data.ac_output_rating_apparent_power, 
                &piri_data.ac_output_rating_active_power, &piri_data.battery_rating_voltage, &piri_data.battery_recharge_voltage, 
                &piri_data.battery_under_voltage, &piri_data.battery_bulk_voltage, &piri_data.battery_float_voltage, 
                &piri_data.battery_type, &piri_data.max_ac_charging_current, &piri_data.max_charging_current); 
        } else { printf("No response from QPIRI.\n"); }
        
        if (query_inverter(handle, "QPIGS2", response) > 0) { printf("QPIGS2 response: %s\n", response); sscanf(response + 1, "%f %f %d", &pigs2_data.pv2_input_current, &pigs2_data.pv2_input_voltage, &pigs2_data.pv2_power); } else { printf("No response from QPIGS2.\n"); }
        if (query_inverter(handle, "QMOD", response) > 0) { printf("QMOD response: %s\n", response); qmod_char = response[1]; } else { printf("No response from QMOD.\n"); }
        if (query_inverter(handle, "QID", response) > 0) { printf("QID response: %s\n", response); strncpy(qid_response, response + 1, sizeof(qid_response) - 1); } else { printf("No response from QID.\n"); }
        if (query_inverter(handle, "QPIGS", response) > 0) { printf("QPIGS response: %s\n", response); sscanf(response + 1, "%f %f %f %f %d %d %d %d %f %d %d %d %f %f %f %d %8s %d %d %d %3s %*s", &pigs_data.grid_voltage, &pigs_data.grid_frequency, &pigs_data.output_voltage, &pigs_data.output_frequency, &pigs_data.output_apparent_power, &pigs_data.output_active_power, &pigs_data.output_load_percent, &pigs_data.bus_voltage, &pigs_data.battery_voltage, &pigs_data.battery_charge_current, &pigs_data.battery_capacity_percent, &pigs_data.inverter_heat_sink_temp, &pigs_data.pv1_input_current, &pigs_data.pv1_input_voltage, &pigs_data.battery_voltage_scc, &pigs_data.battery_discharge_current, pigs_data.device_status, &pigs_data.battery_voltage_offset, &pigs_data.eeprom_version, &pigs_data.pv1_power, pigs_data.device_status_2); } else { printf("No response from QPIGS.\n"); }

        // --- STEP 2: ATOMIC PUBLISHING ---
        printf("Publishing data to MQTT...\n");
        
        // Publish selected configuration values
        publish_metric(client, mqtt_base_topic, "config", "ac_output_rating_active_power", (sprintf(value_buffer, "%d", piri_data.ac_output_rating_active_power), value_buffer));
        publish_metric(client, mqtt_base_topic, "config", "battery_bulk_voltage", (sprintf(value_buffer, "%.1f", piri_data.battery_bulk_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "config", "battery_float_voltage", (sprintf(value_buffer, "%.1f", piri_data.battery_float_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "config", "max_charging_current", (sprintf(value_buffer, "%d", piri_data.max_charging_current), value_buffer));
        
        // Publish serial number if available
        if (strcmp(qid_response, "NAK") != 0) publish_metric(client, mqtt_base_topic, "general", "serial_number", qid_response);
        
        // Publish inverter mode (translated to string)
        char* mode_str = "unknown"; if(qmod_char == 'P') mode_str = "power_on"; else if(qmod_char == 'S') mode_str = "standby"; else if(qmod_char == 'L') mode_str = "line"; else if(qmod_char == 'B') mode_str = "battery"; else if(qmod_char == 'F') mode_str = "fault";
        publish_metric(client, mqtt_base_topic, "general", "device_mode", mode_str);
        
        // Publish PV1 (first solar input) data
        publish_metric(client, mqtt_base_topic, "pv1", "pv1_input_voltage", (sprintf(value_buffer, "%.1f", pigs_data.pv1_input_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "pv1", "pv1_input_current", (sprintf(value_buffer, "%.1f", pigs_data.pv1_input_current), value_buffer));
        publish_metric(client, mqtt_base_topic, "pv1", "pv1_power", (sprintf(value_buffer, "%d", pigs_data.pv1_power), value_buffer));

        // Publish PV2 (second solar input) data
        publish_metric(client, mqtt_base_topic, "pv2", "pv2_input_voltage", (sprintf(value_buffer, "%.1f", pigs2_data.pv2_input_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "pv2", "pv2_input_current", (sprintf(value_buffer, "%.1f", pigs2_data.pv2_input_current), value_buffer));
        publish_metric(client, mqtt_base_topic, "pv2", "pv2_power", (sprintf(value_buffer, "%d", pigs2_data.pv2_power), value_buffer));
        
        // Publish general inverter data
        publish_metric(client, mqtt_base_topic, "general", "grid_voltage", (sprintf(value_buffer, "%.1f", pigs_data.grid_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "output_voltage", (sprintf(value_buffer, "%.1f", pigs_data.output_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "output_active_power", (sprintf(value_buffer, "%d", pigs_data.output_active_power), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "output_load_percent", (sprintf(value_buffer, "%d", pigs_data.output_load_percent), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "battery_voltage", (sprintf(value_buffer, "%.2f", pigs_data.battery_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "battery_capacity_percent", (sprintf(value_buffer, "%d", pigs_data.battery_capacity_percent), value_buffer));
        publish_metric(client, mqtt_base_topic, "general", "inverter_heat_sink_temp", (sprintf(value_buffer, "%d", pigs_data.inverter_heat_sink_temp), value_buffer));
        
        // Calculate and publish total solar power (PV1 + PV2)
        int total_solar_watt = pigs_data.pv1_power + pigs2_data.pv2_power;
        publish_metric(client, mqtt_base_topic, "general", "total_solar_power", (sprintf(value_buffer, "%d", total_solar_watt), value_buffer));

        // --- STEP 3: PAUSE ---
        printf("Cycle finished. Waiting for %d seconds.\n", interval_seconds);
        printf("----------------------------------------\n");
        sleep(interval_seconds); // Wait before next cycle
    }
    
    // --- CLEANUP (never reached in normal operation) ---
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    hid_close(handle);
    hid_exit();
    return 0;
}
// End of file