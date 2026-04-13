# Voltronic MQTT RPi

## Description

`voltronic-mqtt-rpi` is a lightweight C-based monitoring tool for Voltronic Power inverters (such as Axpert, MPP Solar, etc.) that use the serial-over-USB (HID) communication protocol.

The program is designed to run on low-resource systems like the Raspberry Pi, with a special focus on compatibility with **Venus OS** (32-bit ARMv7).

It reads key data from the inverter (status, voltages, currents, power) and publishes it to an MQTT broker, allowing for easy integration into home automation systems like Home Assistant or VenusOS dashboards.

## Project Status (v2.3.0 — Current Release)

-   [x] Communicates with the inverter via its USB HID interface.
-   [x] **MQTT Auto-Reconnect**: Automatically detects broker disconnection and attempts to reconnect without stopping the inverter polling.
-   [x] **Auto-detects HID packet size quirk** — some inverter firmware refuse HID writes > 8 bytes. The program probes at startup and automatically enables split-write mode if needed.
-   [x] **Reliably reads PV2 (second MPPT input)** via the `QPIGS2` command using split-write when required.
-   [x] Performs an initial handshake with quirk detection (`has_pv2` + `use_split_write` flags set once at startup).
-   [x] Reads data from **QPIGS**, **QPIGS2**, **QPIRI**, **QMOD**, **QID** commands.
-   [x] Publishes all data in **Solpiplog-compatible MQTT topic structure** (`.../pip/acin`, `.../pip/battv`, etc.) for seamless drop-in replacement.
-   [x] Computes and publishes `totalsolarw` (PV1 + PV2 combined power).
-   [x] Can be compiled in semi-static mode for maximum portability on Venus OS.

### Changelog

#### v2.3.0 (Current — MQTT Robustness)
- **New**: Implemented automatic MQTT reconnection logic.
- **Refactor**: Renamed source file to a version-independent `voltronic-mqtt-rpi_v2.c`.
- **Improved**: Added connection health checks at the start of every polling cycle.

#### v2.2.2 (Bugfix + Solpiplog compatibility)
- **Fix**: Buffer overflow on `status_str` when mode is `"line"` (4 chars + `\0` = 5 bytes needed, was 4).
- **New**: Topic structure rewritten to match Solpiplog format (`pip/masterstatus`, `pip/status`, `pip/acin`, etc.).
- PV2 publishing now conditional on `has_pv2` flag.

## Compilation

The program depends on the `hidapi` and `paho-mqtt` libraries. On a Debian-based system (like Raspberry Pi OS), install the required development packages:

```bash
sudo apt-get update
sudo apt-get install build-essential libhidapi-dev libpaho-mqtt-dev libusb-1.0-0-dev libudev-dev
```

### Compilation (Recommended via Makefile)
```bash
make
```
This will produce the `voltronic-mqtt` executable.

### Manual Compilation for Raspberry Pi OS (Dynamic Linking)
```bash
gcc voltronic-mqtt-rpi_v2.c -o voltronic-mqtt -lhidapi-libusb -lpaho-mqtt3c
```

### Compilation for Venus OS (Semi-Static Linking)
```bash
make venus
```

## Usage

```bash
sudo ./voltronic-mqtt <mqtt_host> <mqtt_user> <mqtt_pass> <base_topic> <interval_sec>
```

**Example:**
```bash
sudo ./voltronic-mqtt 192.168.1.100 myuser mypassword Inverter 5
```

This will publish topics like:
- `Inverter/pip/masterstatus` → `Line`
- `Inverter/pip/battv` → `26.40`
- `Inverter/pip/pvvolt` → `142.3`
- `Inverter/pip/totalsolarw` → `850`

## To-Do and Future Development
-   [ ] HID reconnect on inverter communication error (currently requires restart).
-   [ ] Decode `device_status` bitfield to publish individual boolean states.
-   [ ] Optional systemd service file for auto-start on boot.

## License

This project is released under the **MIT License**. See the `LICENSE` file for more details.