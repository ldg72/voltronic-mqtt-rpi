# Makefile for the Voltronic MQTT RPi project — v2.2.2

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -O2

# Source file and target executables
SRC = voltronic-mqtt-rpi_v2.2.2.c
TARGET_DYNAMIC = voltronic-mqtt
TARGET_VENUS = voltronic-rpi-venus

# --- Libraries ---
# Static library paths (adjust if your system differs)
HIDAPI_STATIC = /usr/lib/arm-linux-gnueabihf/libhidapi-libusb.a
PAHO_STATIC = /usr/lib/arm-linux-gnueabihf/libpaho-mqtt3c.a

# Dynamic library flags
LDFLAGS_DYNAMIC = -lhidapi-libusb -lpaho-mqtt3c
# System library dependencies for the semi-static build
LDFLAGS_STATIC_DEPS = -lusb-1.0 -ludev -lpthread -lrt


# --- Build Rules ---
.PHONY: all clean venus

# Default: dynamic build for Raspberry Pi OS
all: $(TARGET_DYNAMIC)

$(TARGET_DYNAMIC): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_DYNAMIC) $(LDFLAGS_DYNAMIC)
	@echo "\nSuccessfully built dynamic target: $(TARGET_DYNAMIC)"

# Semi-static build for Venus OS (32-bit ARMv7)
venus: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_VENUS) $(HIDAPI_STATIC) $(PAHO_STATIC) $(LDFLAGS_STATIC_DEPS)
	@echo "\nSuccessfully built semi-static target for Venus OS: $(TARGET_VENUS)"

clean:
	rm -f $(TARGET_DYNAMIC) $(TARGET_VENUS)
	@echo "Cleaned up build files."