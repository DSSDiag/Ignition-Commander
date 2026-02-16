# Smart Compressor Controller (ESP32C3 + RainMaker)

This project allows you to schedule your air compressor's power supply using a **Seeed Studio XIAO ESP32C3** and a Solid State Relay (SSR). It integrates with **Google Home** via **ESP RainMaker**.

## Features
*   **Automated Scheduling:** Set Start and End times.
*   **Offline Reliability:** The schedule runs locally on the ESP32. It only needs the internet to sync the time (NTP).
*   **Safety Cutoff:** If the power fails and the device cannot sync the time (no internet), the compressor stays **OFF** to prevent accidents.
*   **Factory Reset:** Easily reset Wi-Fi provisioning and clear preferences by holding the BOOT button for 3 seconds.
*   **Google Home Integration:** Control the compressor (On/Off) using Google Assistant.
*   **App Control:** Use the ESP RainMaker app to change schedules and monitor status.

## Hardware Required
1.  **Seeed XIAO ESP32C3** (or any ESP32C3 Mini board).
2.  **Solid State Relay (SSR)**: Capable of handling your compressor's voltage/current.
3.  **Wires**: For connecting the ESP32 to the SSR.
4.  **Power Supply**: 5V USB-C for the ESP32.

## Wiring

| XIAO ESP32C3 Pin | SSR Connection |
| :--- | :--- |
| **D0 (GPIO 2)** | **Control / Signal (+)** |
| **GND** | **Ground (-)** |
| **5V / VBUS** | **VCC** (If your SSR needs power) |
| **D9 (Boot Button)** | **Factory Reset** (Built-in) |

> **WARNING:** You are working with mains voltage (110V/220V) when connecting the SSR to the compressor. Ensure the compressor is unplugged before working on it. Isolate all high-voltage connections properly.

## Software Setup (PlatformIO)

This project uses **PlatformIO**.

### 1. Prerequisites
*   Visual Studio Code
*   PlatformIO IDE Extension

### 2. Build and Upload
1.  Open this folder in VS Code.
2.  Click the PlatformIO Alien icon in the sidebar.
3.  Under `env:seeed_xiao_esp32c3`, click **Build** to verify.
4.  Connect your board via USB.
5.  Click **Upload**.
6.  Click **Monitor** to see the Serial output and provisioning QR code.

## Usage Instructions

### 1. Initial Setup (Provisioning)
1.  Download the **ESP RainMaker** app (iOS/Android).
2.  Open the Serial Monitor.
3.  Reset the ESP32. You should see a message saying "Provisioning Started".
4.  Open the RainMaker app and tap **"Add Device"**.
5.  It should automatically detect the device via Bluetooth (BLE).
6.  Follow the steps to connect it to your Wi-Fi.

### 2. Configuration
Once added, you will see a "Compressor" device.
*   **Power Switch:** Manually turn on/off.
*   **Enable Schedule:** Toggle to enable automatic control.
*   **Start/End Hour/Minute:** Set your daily window.
*   **Timezone Offset:** Set your offset from UTC (e.g., -5 for EST, -8 for PST).

### 3. Google Home Integration
1.  Open the **Google Home** app.
2.  Tap **+ (Add)** > **Set up device** > **Works with Google**.
3.  Search for **ESP RainMaker**.
4.  Login with the same account you used in the RainMaker app.
5.  Your compressor will appear as a Switch!

### 4. Factory Reset
If you change Wi-Fi networks or encounter issues:
1.  Press and hold the **BOOT Button (GPIO 9)** on the XIAO ESP32C3 for **3 seconds**.
2.  Release the button when the Serial Monitor indicates "Factory Reset Triggered" (or verify reset behavior).
3.  The device will clear its Wi-Fi credentials and reboot into Provisioning Mode.

## Safety Notes
*   **Time Validation:** The device defaults to **OFF** if it cannot determine the current time via NTP (e.g., year < 2022). This ensures the compressor doesn't run unexpectedly if the internet is down after a power outage.
*   Always ensure your SSR is rated for the motor load (Inductive load).
