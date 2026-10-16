# Tongou ESP32-S3 Local Controller

A standalone ESP32-S3 controller for the **Tongou TO-Q-SY1-WT-DC Wi-Fi DIN rail switch**, providing local relay control through a lightweight web interface and direct communication using the Tuya 3.5 protocol.

The project creates its own Wi-Fi access point, allowing a phone, tablet, or computer to connect and control the switch from a browser without requiring a dedicated mobile application or cloud-based control service.

## Features

- **Standalone web interface** — accessible at `http://192.168.4.1/`.
- **Local ON/OFF control** — operate the Tongou relay directly from a browser.
- **Relay state verification** — query the device and confirm the actual relay state after a command.
- **Tuya 3.5 protocol** — encrypted local communication using AES-128-GCM and HMAC-SHA256.
- **Automatic device discovery** — identifies the switch by its MAC address and attempts to obtain its IP address through DHCP and ARP.
- **Wi-Fi access point** — creates the `SWITCHAP` network for local access.
- **Internet sharing** — supports upstream Wi-Fi connectivity and NAPT routing.
- **Lightweight implementation** — built with the Arduino framework for ESP32.

## Hardware and software

- ESP32-S3 development board
- Tongou TO-Q-SY1-WT-DC Wi-Fi DIN rail switch
- Arduino IDE 2.3.7
- Arduino-ESP32 core 3.3.12

## Getting started

1. Configure the upstream Wi-Fi credentials and the Tongou device's ID and local key in the sketch.
2. Compile and upload the firmware to the ESP32-S3.
3. Connect your phone or computer to the `SWITCHAP` Wi-Fi network.
4. Open `http://192.168.4.1/` in a web browser.
5. Use the ON, OFF, and Query buttons to control the relay and verify its state.

## Security

This project uses local network communication and requires the correct Tuya device credentials. Store Wi-Fi passwords and local keys securely. Do not commit personal credentials or secrets to a public repository; use placeholders in published code.

## Project status

Functional prototype with working browser-based relay control, state queries, and relay state verification. Device discovery and connectivity depend on the switch's network behaviour and firmware.
