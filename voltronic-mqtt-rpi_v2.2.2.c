// voltronic-mqtt-rpi v2.2.2 (Personal build for Luca - Bugfix release)
// Authors: Luca De Giovanni & Google AI Studio
// Date: July 28, 2024
// Description: Drop-in replacement for Solpiplog, replicating its MQTT topic structure.
//              Fixes a buffer overflow warning found during compilation.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <MQTTClient.h>
#include <time.h>

#define MAX_BUFFER_SIZE 512
#define INVERTER_VID 0x0665
#define INVERTER_PID 0x5161

// Global state variable: determines if split-write for commands > 8 bytes is needed.
int use_split_write = 0;

// --- DATA STRUCTURES ---
typedef struct { float grid_voltage, grid_frequency, output_voltage, output_frequency; int output_apparent_power, output_active_power, output_load_percent, bus_voltage; float battery_voltage; int battery_charge_current, battery_capacity_percent, inverter_heat_sink_temp; float pv1_input_current, pv1_input_voltage; float battery_voltage_scc; int battery_discharge_current; char device_status[9]; int battery_voltage_offset, eeprom_version, pv1_power; char device_status_2[4]; } PIGSData;
typedef struct { float pv2_input_current, pv2_input_voltage; int pv2_power; } PIGS2Data;
typedef struct { float grid_rating_voltage, grid_rating_current, ac_output_rating_voltage, ac_output_rating_frequency, ac_output_rating_current; int ac_output_rating_apparent_power, ac_output_rating_active_power; float battery_rating_voltage, battery_recharge_voltage, battery_under_voltage, battery_bulk_voltage, battery_float_voltage; int battery_type, max_ac_charging_current, max_charging_current; } PIRIData;

// --- HELPER FUNCTIONS ---
unsigned short crc16(const unsigned char *buf, size_t len) { unsigned short crc = 0; while (len--) { crc ^= *buf++ << 8; for (int i = 0; i < 8; i++) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1; } return crc; }
void publish_metric(MQTTClient client, const char* base_topic, const char* sub_topic, const char* metric_name, const char* value_str) { if (value_str == NULL || strlen(value_str) == 0) return; char full_topic[256]; sprintf(full_topic, "%s/%s/%s", base_topic, sub_topic, metric_name); MQTTClient_message pubmsg = MQTTClient_message_initializer; pubmsg.payload = (void*)value_str; pubmsg.payloadlen = strlen(value_str); pubmsg.qos = 1; pubmsg.retained = 1; MQTTClient_publishMessage(client, full_topic, &pubmsg, NULL); }

// Robust query function that handles HID packet splitting
int query_inverter(hid_device *handle, const char *command, char *response, int is_test) {
    unsigned char dummy_buffer[64];
    if (!is_test) {
        while(hid_read_timeout(handle, dummy_buffer, sizeof(dummy_buffer), 50) > 0);
    }
    
    unsigned char buffer_to_send[MAX_BUFFER_SIZE];
    unsigned short crc = crc16((unsigned char*)command, strlen(command));
    int len = sprintf((char*)buffer_to_send, "%s%c%c\r", command, (crc & 0xff), (crc >> 8));

    if (use_split_write && len > 8) {
        int bytes_sent = 0;
        while (bytes_sent < len) {
            int chunk_size = (len - bytes_sent > 8) ? 8 : (len - bytes_sent);
            if (hid_write(handle, buffer_to_send + bytes_sent, chunk_size) < 0) { fprintf(stderr, "hid_write error (split) for %s\n", command); return -1; }
            bytes_sent += chunk_size;
            if (bytes_sent < len) usleep(50000);
        }
    } else {
        if (hid_write(handle, buffer_to_send, len) < 0) { fprintf(stderr, "hid_write error (standard) for %s\n", command); return -1; }
    }

    int total_bytes = 0;
    time_t start_time = time(NULL);
    while (difftime(time(NULL), start_time) < 3.0) {
        int rc = hid_read_timeout(handle, (unsigned char*)response + total_bytes, MAX_BUFFER_SIZE - total_bytes - 1, 200);
        if (rc > 0) {
            total_bytes += rc;
            if (memchr(response, '\r', total_bytes) != NULL) {
                char *end_ptr = memchr(response, '\r', total_bytes);
                if (end_ptr && (end_ptr - response) > 2) { *(end_ptr - 2) = '\0'; } else { response[0] = '\0'; }
                return total_bytes;
            }
        } else if (rc < 0) { fprintf(stderr, "hid_read error for %s\n", command); return -1; }
    }
    response[0] = '\0';
    return 0;
}

// --- MAIN PROGRAM ---
int main(int argc, char* argv[]) {
    if (argc < 6) { fprintf(stderr, "Usage: %s <host> <user> <pass> <base_topic> <interval>\n", argv[0]); return 1; }
    char* mqtt_host = argv[1], *mqtt_user = argv[2], *mqtt_pass = argv[3], *mqtt_base_topic = argv[4];
    int interval_seconds = atoi(argv[5]);

    hid_init();
    hid_device *handle = hid_open(INVERTER_VID, INVERTER_PID, NULL);
    if (!handle) { fprintf(stderr, "Unable to open HID device.\n"); return 1; }
    printf("HID device opened successfully.\n");
    sleep(2);

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    char mqtt_address[100]; sprintf(mqtt_address, "tcp://%s:1883", mqtt_host);
    MQTTClient_create(&client, mqtt_address, "voltronic-rpi-client", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20; conn_opts.cleansession = 1; conn_opts.username = mqtt_user; conn_opts.password = mqtt_pass;
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) { fprintf(stderr, "Failed to connect to MQTT broker.\n"); return 1; }
    printf("Connected to MQTT broker: %s\n", mqtt_host);
    
    char response[MAX_BUFFER_SIZE];
    char value_buffer[50];
    
    // --- HANDSHAKE & QUIRK DETECTION ---
    printf("\n--- Starting Handshake & Quirk Detection ---\n");
    int has_pv2 = 0;
    printf("Probing for QPIGS2 with standard write method...\n");
    if (query_inverter(handle, "QPIGS2", response, 1) > 0) {
        printf("Standard method successful. Inverter is fully compatible.\n");
        has_pv2 = 1;
        use_split_write = 0;
    } else {
        printf("Standard method failed. Probing for HID packet size quirk...\n");
        use_split_write = 1;
        if (query_inverter(handle, "QPIGS2", response, 1) > 0) {
            printf("Split-write method successful. Quirk detected and enabled.\n");
            has_pv2 = 1;
        } else {
            printf("Split-write method also failed. Inverter likely has no PV2 input.\n");
            has_pv2 = 0;
            use_split_write = 0;
        }
    }
    printf("--- Handshake Finished ---\n\n");
    
    // --- MAIN POLLING LOOP ---
    while (1) {
        printf("Starting reading cycle...\n");
        PIRIData piri_data = {0}; PIGS2Data pigs2_data = {0}; char qmod_char = ' ';
        char qid_response[32] = "N/A"; PIGSData pigs_data = {0};
        
        // --- DATA GATHERING ---
        if (query_inverter(handle, "QPIRI", response, 0) > 0) { printf("QPIRI response: %s\n", response); sscanf(response + 1, "%f %f %f %f %f %d %d %f %f %f %f %f %d %d %d", &piri_data.grid_rating_voltage, &piri_data.grid_rating_current, &piri_data.ac_output_rating_voltage, &piri_data.ac_output_rating_frequency, &piri_data.ac_output_rating_current, &piri_data.ac_output_rating_apparent_power, &piri_data.ac_output_rating_active_power, &piri_data.battery_rating_voltage, &piri_data.battery_recharge_voltage, &piri_data.battery_under_voltage, &piri_data.battery_bulk_voltage, &piri_data.battery_float_voltage, &piri_data.battery_type, &piri_data.max_ac_charging_current, &piri_data.max_charging_current); } else { printf("No response from QPIRI.\n"); }
        if (has_pv2) { if (query_inverter(handle, "QPIGS2", response, 0) > 0) { printf("QPIGS2 response: %s\n", response); sscanf(response + 1, "%f %f %d", &pigs2_data.pv2_input_current, &pigs2_data.pv2_input_voltage, &pigs2_data.pv2_power); } else { printf("No response from QPIGS2.\n"); } }
        if (query_inverter(handle, "QMOD", response, 0) > 0) { printf("QMOD response: %s\n", response); qmod_char = response[1]; } else { printf("No response from QMOD.\n"); }
        if (query_inverter(handle, "QID", response, 0) > 0) { printf("QID response: %s\n", response); strncpy(qid_response, response + 1, sizeof(qid_response) - 1); } else { printf("No response from QID.\n"); }
        if (query_inverter(handle, "QPIGS", response, 0) > 0) { printf("QPIGS response: %s\n", response); sscanf(response + 1, "%f %f %f %f %d %d %d %d %f %d %d %d %f %f %f %d %8s %d %d %d %3s %*s", &pigs_data.grid_voltage, &pigs_data.grid_frequency, &pigs_data.output_voltage, &pigs_data.output_frequency, &pigs_data.output_apparent_power, &pigs_data.output_active_power, &pigs_data.output_load_percent, &pigs_data.bus_voltage, &pigs_data.battery_voltage, &pigs_data.battery_charge_current, &pigs_data.battery_capacity_percent, &pigs_data.inverter_heat_sink_temp, &pigs_data.pv1_input_current, &pigs_data.pv1_input_voltage, &pigs_data.battery_voltage_scc, &pigs_data.battery_discharge_current, pigs_data.device_status, &pigs_data.battery_voltage_offset, &pigs_data.eeprom_version, &pigs_data.pv1_power, pigs_data.device_status_2); } else { printf("No response from QPIGS.\n"); }

        // --- ATOMIC PUBLISHING (Solpiplog compatibility version) ---
        printf("Publishing data to MQTT...\n");
        const char* sub_topic = "pip";

        char* masterstatus_str = "Unknown"; if(qmod_char == 'P') masterstatus_str = "Power On"; else if(qmod_char == 'S') masterstatus_str = "Standby"; else if(qmod_char == 'L') masterstatus_str = "Line"; else if(qmod_char == 'B') masterstatus_str = "Battery"; else if(qmod_char == 'F') masterstatus_str = "Fault";
        publish_metric(client, mqtt_base_topic, sub_topic, "masterstatus", masterstatus_str);
        
        // *** FIX v2.2.2: Increased buffer size to 5 to hold "line" + null terminator '\0' ***
        char status_str[5] = "unk"; 
        if(qmod_char == 'L') strcpy(status_str, "line"); else if(qmod_char == 'B') strcpy(status_str, "bat"); else if(qmod_char == 'F') strcpy(status_str, "flt");
        publish_metric(client, mqtt_base_topic, sub_topic, "status", status_str);
        
        publish_metric(client, mqtt_base_topic, sub_topic, "status2", pigs_data.device_status_2);
        publish_metric(client, mqtt_base_topic, sub_topic, "charge", (sprintf(value_buffer, "%d", piri_data.battery_type), value_buffer));
        if (strcmp(qid_response, "NAK") != 0) publish_metric(client, mqtt_base_topic, sub_topic, "serial", qid_response);
        publish_metric(client, mqtt_base_topic, sub_topic, "acin", (sprintf(value_buffer, "%.1f", pigs_data.grid_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "acinhz", (sprintf(value_buffer, "%.1f", pigs_data.grid_frequency), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "acout", (sprintf(value_buffer, "%.1f", pigs_data.output_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "acouthz", (sprintf(value_buffer, "%.1f", pigs_data.output_frequency), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "acoutva", (sprintf(value_buffer, "%d", pigs_data.output_apparent_power), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "acoutw", (sprintf(value_buffer, "%d", pigs_data.output_active_power), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "load", (sprintf(value_buffer, "%d", pigs_data.output_load_percent), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "busv", (sprintf(value_buffer, "%d", pigs_data.bus_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "battv", (sprintf(value_buffer, "%.2f", pigs_data.battery_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "battchrg", (sprintf(value_buffer, "%d", pigs_data.battery_charge_current), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "battcappa", (sprintf(value_buffer, "%d", pigs_data.battery_capacity_percent), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "heatsinktemp", (sprintf(value_buffer, "%d", pigs_data.inverter_heat_sink_temp), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "battvscc", (sprintf(value_buffer, "%.2f", pigs_data.battery_voltage_scc), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "battdischrg", (sprintf(value_buffer, "%d", pigs_data.battery_discharge_current), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "devicestatus", pigs_data.device_status);
        publish_metric(client, mqtt_base_topic, sub_topic, "pvibatt", (sprintf(value_buffer, "%.1f", pigs_data.pv1_input_current), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "pvvolt", (sprintf(value_buffer, "%.1f", pigs_data.pv1_input_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "pvchargew", (sprintf(value_buffer, "%d", pigs_data.pv1_power), value_buffer));
        if (has_pv2) {
            publish_metric(client, mqtt_base_topic, sub_topic, "pv2ibatt", (sprintf(value_buffer, "%.1f", pigs2_data.pv2_input_current), value_buffer));
            publish_metric(client, mqtt_base_topic, sub_topic, "pv2volt", (sprintf(value_buffer, "%.1f", pigs2_data.pv2_input_voltage), value_buffer));
            publish_metric(client, mqtt_base_topic, sub_topic, "pv2chargew", (sprintf(value_buffer, "%d", pigs2_data.pv2_power), value_buffer));
        }
        int total_solar_watt = pigs_data.pv1_power + (has_pv2 ? pigs2_data.pv2_power : 0);
        publish_metric(client, mqtt_base_topic, sub_topic, "totalsolarw", (sprintf(value_buffer, "%d", total_solar_watt), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "bulk", (sprintf(value_buffer, "%.1f", piri_data.battery_bulk_voltage), value_buffer));
        publish_metric(client, mqtt_base_topic, sub_topic, "float", (sprintf(value_buffer, "%.1f", piri_data.battery_float_voltage), value_buffer));

        printf("Cycle finished. Waiting for %d seconds.\n----------------------------------------\n", interval_seconds);
        sleep(interval_seconds);
    }
    
    MQTTClient_disconnect(client, 10000); MQTTClient_destroy(&client);
    hid_close(handle); hid_exit();
    return 0;
}