# Smart Compressor Controller (ESP32C3 + RainMaker)

This project allows you to schedule your air compressor's power supply using a **Seeed Studio XIAO ESP32C3** and a Solid State Relay (SSR). It integrates with **Google Home** via **ESP RainMaker**.

## Features
*   **Automated Scheduling:** Set Start and End times.
*   **Offline Reliability:** The schedule runs locally on the ESP32. It only needs the internet to sync the time (NTP).
*   **Safety Cutoff:** If the power fails and the device cannot sync the time (no internet), the compressor stays **OFF** to prevent accidents.
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

> **WARNING:** You are working with mains voltage (110V/220V) when connecting the SSR to the compressor. Ensure the compressor is unplugged before working on it. Isolate all high-voltage connections properly.

## Software Setup (Arduino IDE / VS Code)

### 1. Install ESP32 Board Manager
1.  Open **Preferences**.
2.  Add this URL to **Additional Board Manager URLs**:
    ```
    https://espressif.github.io/arduino-esp32/package_esp32_index.json
    ```
3.  Go to **Tools > Board > Boards Manager**, search for `esp32` (by Espressif Systems), and install it.

### 2. Select Your Board
*   **Board:** `Seeed XIAO ESP32C3` (or `ESP32C3 Dev Module`).
*   **USB CDC On Boot:** `Enabled` (Crucial for seeing Serial logs on the C3).
*   **Partition Scheme:** `RainMaker` (You MUST select this, or the code won't fit).
    *   *If "RainMaker" is not available, try "Huge App".*

### 3. Install Libraries
Go to **Tools > Manage Libraries** and install:
*   `ESP RainMaker` by Espressif Systems.

### 4. Upload
1.  Connect your ESP32C3 via USB.
2.  Select the correct COM port.
3.  Click **Upload**.

## Usage Instructions

### 1. Initial Setup (Provisioning)
1.  Download the **ESP RainMaker** app (iOS/Android).
2.  Open the Serial Monitor (Baud 115200) on your computer.
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

## Safety Notes
*   The device defaults to **OFF** if it cannot determine the current time (e.g., year < 2022).
*   Always ensure your SSR is rated for the motor load (Inductive load).
