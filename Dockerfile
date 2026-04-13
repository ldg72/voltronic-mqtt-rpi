# 1. Specifichiamo che vogliamo un sistema identico al Raspberry Pi 3 (ARM 32-bit)
FROM arm32v7/debian:bookworm

# 2. Impediamo che il sistema faccia domande durante l'installazione (per farlo andare in automatico)
ENV DEBIAN_FRONTEND=noninteractive

# 3. Lanciamo i comandi di installazione, proprio come faresti sul Raspberry
#    Installiamo: build-essential (gcc e make) e le librerie dell'inverter
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    libhidapi-dev \
    libpaho-mqtt-dev \
    libusb-1.0-0-dev \
    libudev-dev \
    && rm -rf /var/lib/apt/lists/*

# 4. Diciamo a Docker che lavoreremo nella cartella /app
WORKDIR /app
