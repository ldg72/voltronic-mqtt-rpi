# Makefile for the Voltronic MQTT RPi project

# Compiler and flags
CC = gcc
# CFLAGS: -Wall and -Wextra enable most warnings, -g adds debug symbols, -O2 optimizes the code
CFLAGS = -Wall -Wextra -g -O2

# Source file and target executables
SRC = voltronic-mqtt2.1.2.c
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

# .PHONY tells make that these are command names, not files
.PHONY: all clean venus

# Default command (typing 'make' will run this)
# Builds the standard dynamic version for Raspberry Pi OS
all: $(TARGET_DYNAMIC)

# Rule to build the dynamic version
$(TARGET_DYNAMIC): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_DYNAMIC) $(LDFLAGS_DYNAMIC)
	@echo "\nSuccessfully built dynamic target: $(TARGET_DYNAMIC)"

# Rule to build the semi-static version for Venus OS
venus: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_VENUS) $(HIDAPI_STATIC) $(PAHO_STATIC) $(LDFLAGS_STATIC_DEPS)
	@echo "\nSuccessfully built semi-static target for Venus OS: $(TARGET_VENUS)"

# Rule to clean up compiled files
clean:
	rm -f $(TARGET_DYNAMIC) $(TARGET_VENUS)
	@echo "Cleaned up build files."