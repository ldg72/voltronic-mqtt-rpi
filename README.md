# Voltronic MQTT RPi

## Description

`voltronic-mqtt-rpi` is a lightweight C-based monitoring tool for Voltronic Power inverters (such as Axpert, MPP Solar, etc.) that use the serial-over-USB (HID) communication protocol.

The program is designed to run on low-resource systems like the Raspberry Pi, with a special focus on compatibility with **Venus OS** (32-bit ARMv7).

It reads key data from the inverter (status, voltages, currents, power) and publishes it to an MQTT broker, allowing for easy integration into home automation systems like Home Assistant or VenusOS dashboards.

## Project Status (v2.2.2 — Current Release)

-   [x] Communicates with the inverter via its USB HID interface.
-   [x] **Auto-detects HID packet size quirk** — some inverter firmware refuse HID writes > 8 bytes. The program probes at startup and automatically enables split-write mode if needed.
-   [x] **Reliably reads PV2 (second MPPT input)** via the `QPIGS2` command using split-write when required.
-   [x] Performs an initial handshake with quirk detection (`has_pv2` + `use_split_write` flags set once at startup).
-   [x] Reads data from **QPIGS**, **QPIGS2**, **QPIRI**, **QMOD**, **QID** commands.
-   [x] Publishes all data in **Solpiplog-compatible MQTT topic structure** (`.../pip/acin`, `.../pip/battv`, etc.) for seamless drop-in replacement.
-   [x] Computes and publishes `totalsolarw` (PV1 + PV2 combined power).
-   [x] Can be compiled in semi-static mode for maximum portability on Venus OS.

### Changelog

#### v2.2.2 (Current — Bugfix + Solpiplog compatibility)
- **Fix**: Buffer overflow on `status_str` when mode is `"line"` (4 chars + `\0` = 5 bytes needed, was 4).
- **New**: Topic structure rewritten to match Solpiplog format (`pip/masterstatus`, `pip/status`, `pip/acin`, etc.).
- PV2 publishing now conditional on `has_pv2` flag (no wasted cycles if inverter lacks second MPPT).

#### v2.2.0 (GitHub)
- Introduced adaptive `use_split_write` and `has_pv2` auto-detection at startup.
- **Root fix for PV2 issue**: `QPIGS2` command is 9 bytes (> 8-byte HID limit), requiring split-write to work correctly.

#### v2.1.2 (Previous GitHub release)
- QPIGS2 always queried without split-write → PV2 values always zero on affected firmware.

## Compilation

The program depends on the `hidapi` and `paho-mqtt` libraries. On a Debian-based system (like Raspberry Pi OS), install the required development packages:

```bash
sudo apt-get update
sudo apt-get install build-essential libhidapi-dev libpaho-mqtt-dev libusb-1.0-0-dev libudev-dev
```

### Compilation for Raspberry Pi OS (Dynamic Linking)
```bash
gcc voltronic-mqtt-rpi_v2.2.2.c -o voltronic-mqtt -lhidapi-libusb -lpaho-mqtt3c
```

Or simply:
```bash
make
```

### Compilation for Venus OS (Semi-Static Linking)
This is the recommended method for portability on Venus OS (32-bit ARMv7).

```bash
make venus
```

Or manually:
```bash
gcc voltronic-mqtt-rpi_v2.2.2.c -o voltronic-rpi-venus \
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
sudo ./voltronic-mqtt 192.168.1.100 myuser mypassword inverter 5
```

This will publish topics like:
- `inverter/pip/masterstatus` → `Line`
- `inverter/pip/battv` → `26.40`
- `inverter/pip/pvvolt` → `142.3`
- `inverter/pip/pv2volt` → `138.7`
- `inverter/pip/totalsolarw` → `850`

## To-Do and Future Development
-   [ ] MQTT reconnect with exponential backoff (currently exits on broker disconnect).
-   [ ] HID reconnect on inverter communication error (currently requires restart).
-   [ ] Decode `device_status` bitfield to publish individual boolean states.
-   [ ] Optional systemd service file for auto-start on boot.

## License

This project is released under the **MIT License**. See the `LICENSE` file for more details.