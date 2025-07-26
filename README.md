# Voltronic MQTT RPi

## Description

`voltronic-mqtt-rpi` is a lightweight C-based monitoring tool for Voltronic Power inverters (such as Axpert, MPP Solar, etc.) that use the serial-over-USB (HID) communication protocol.

The program is designed to run on low-resource systems like the Raspberry Pi, with a special focus on compatibility with **Venus OS**.

It reads key data from the inverter (status, voltages, currents, power) and publishes it to an MQTT broker, allowing for easy integration into home automation systems like Home Assistant.

## Project Status (v2.1.2)

-   [x] Communicates with the inverter via its USB HID interface.
-   [x] Performs an initial "handshake" to establish a stable communication link.
-   [x] Reliably reads and publishes data from the **QPIGS**, **QPIRI**, and **QID** commands.
-   [x] Attempts to read data from the second MPPT via **QPIGS2** (currently not working on the tested model).
-   [x] Publishes all data to a structured MQTT topic tree (`.../general/`, `.../pv1/`, `.../config/`).
-   [x] Can be compiled in a "semi-static" mode for maximum portability on embedded systems like Venus OS.

## Compilation

The program depends on the `hidapi` and `paho-mqtt` libraries. On a Debian-based system (like Raspberry Pi OS), install the required development packages:

```bash
sudo apt-get update
sudo apt-get install build-essential libhidapi-dev libpaho-mqtt-dev libusb-1.0-0-dev libudev-dev
```

### Compilation for Raspberry Pi OS (Dynamic Linking)
```bash
gcc voltronic-mqtt.c -o voltronic-mqtt -lhidapi-libusb -lpaho-mqtt3c
```

### Compilation for Venus OS (Semi-Static Linking)
This is the recommended method for portability.

```bash
gcc voltronic-mqtt.c -o voltronic-rpi-venus \
/usr/lib/arm-linux-gnueabihf/libhidapi-libusb.a \
/usr/lib/arm-linux-gnueabihf/libpaho-mqtt3c.a \
-lusb-1.0 -ludev -lpthread -lrt
```

## Usage

```bash
sudo ./voltronic-mqtt <mqtt_host> <mqtt_user> <mqtt_pass> <base_topic> <interval_sec>
```

**Example:**
```bash
sudo ./voltronic-mqtt 192.168.1.100 myuser mypassword inverter/pip 5
```

## To-Do and Future Development
-   [ ] Solve the issue of the inverter not responding to the `QPIGS2` command on certain models.
-   [ ] Investigate using a Python-based implementation to compare HID library behavior.
-   [ ] Decode the `device_status` string to publish individual boolean states.

## License

This project is released under the **MIT License**. See the `LICENSE` file for more details.