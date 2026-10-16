## Demonstration

The video below demonstrates the ESP32-S3 controller,
including the web interface, relay ON/OFF control, and
relay state verification.

https://github.com/user-attachments/assets/e722a685-2a01-426b-8be8-312cb22182d8



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


## Obtaining the Tuya Device ID and Local Key

The ESP32 controller requires two values from your Tongou TO-Q-SY1-WT-DC switch:

- **Device ID** — uniquely identifies your switch on the Tuya platform.
- **Local Key** — a 16-character secret used by the ESP32 to establish encrypted local communication with the switch.

You must obtain these values for your own device. They cannot be copied from another user's switch.

### Step 1 — Create a Tuya IoT Developer account

1. Visit the [Tuya IoT Developer Platform](https://platform.tuya.com/).
2. Create an account or sign in.
3. Create a Cloud Project if you do not already have one.
4. Select the appropriate development method for your Tuya or Smart Life devices.
5. Select the data centre region that matches your app account and devices.

### Step 2 — Link your Smart Life or Tuya Smart account

Your Tongou switch must be accessible to the cloud project.

1. Open your cloud project.
2. Navigate to **Devices** and find the option to link an app account.
3. Follow the on-screen instructions to link the account used to pair your switch in the Smart Life or Tuya Smart app.
4. Confirm that your Tongou switch appears in the project's device list.

**Important:** If the switch does not appear, check the linked app account and data centre region. Devices added through a manufacturer's own app may require additional steps or may not be available through the developer platform.

### Step 3 — Find the Device ID

1. Open your cloud project in the Tuya Developer Platform.
2. Navigate to **Devices** and open the device list.
3. Find your Tongou TO-Q-SY1-WT-DC switch.
4. Copy its **Device ID**.

Keep this value for the next step.

### Step 4 — Retrieve the Local Key

1. In the Tuya platform, open **Cloud → API Explorer**.
2. Select the appropriate cloud project and API service.
3. In the API navigation, open **Device Management → Query Device Details in Bulk**.
4. Enter the Device ID from Step 3 in the `device_ids` parameter.
5. Click **Submit Request**.
6. Examine the response and locate the `local_key` field.
7. Copy the value exactly, preserving uppercase and lowercase letters, numbers, and punctuation.

The Local Key should be 16 characters for this ESP32 sketch.

For the official API reference, see [Tuya — Query Device Details in Bulk](https://developer.tuya.com/en/docs/cloud/764ba7cd6e?id=Kcp2l1x91ejp7).

### Step 5 — Add the values to your ESP32 sketch

Replace the placeholders in the configuration section with your own device information:

```cpp
static const char *TUYA_DEVICE_ID = "YOUR_DEVICE_ID";
static const char *TUYA_LOCAL_KEY = "YOUR_16_CHARACTER_LOCAL_KEY";
```

Save the sketch, compile it, and upload it to the ESP32-S3.

### Troubleshooting

- **Device not listed:** Check that the correct Smart Life or Tuya Smart account is linked to your cloud project.
- **No Local Key returned:** Verify the project region, API service permissions, and that the device belongs to the linked account. Tuya may restrict access to local keys for some devices.
- **Authentication fails:** Recheck the Device ID and Local Key, ensuring that the key is copied exactly.
- **Switch was reset or re-paired:** Check whether the device's Local Key has changed and retrieve the current value if necessary.

### Security warning

The Local Key is a sensitive credential. Anyone who obtains it may be able to communicate with or control your switch over the local network, depending on the device and its firmware.

**Never publish your real Local Key, Wi-Fi password, or other credentials in a public GitHub repository.** Use placeholder values in your example code and keep your personal configuration private.
