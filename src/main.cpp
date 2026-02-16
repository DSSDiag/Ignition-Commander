#include <Arduino.h>
#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include "time.h"
#include <Preferences.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// Pins
#define RELAY_PIN       2  // D0 on Seeed XIAO ESP32C3
#define BOOT_BUTTON_PIN 9  // Built-in BOOT button

// Device Info
#define DEVICE_NAME     "Compressor"
#define NODE_NAME       "CompressorNode"

// Default Schedule
#define DEFAULT_START_HR  8
#define DEFAULT_START_MIN 0
#define DEFAULT_END_HR    17
#define DEFAULT_END_MIN   0
#define DEFAULT_TZ_OFFSET 0 // UTC by default

// NTP Server
const char* ntpServer = "pool.ntp.org";

// Factory Reset Hold Time (ms)
#define FACTORY_RESET_TIME_MS 3000

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

// State
bool relay_state = false;

// Schedule Parameters
bool schedule_enabled = false;
int start_hr = DEFAULT_START_HR;
int start_min = DEFAULT_START_MIN;
int end_hr = DEFAULT_END_HR;
int end_min = DEFAULT_END_MIN;
int timezone_offset = DEFAULT_TZ_OFFSET;

// RainMaker Objects
static Switch my_switch(DEVICE_NAME, &relay_state);

// Preferences
Preferences prefs;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
void sysProvEvent(arduino_event_t *sys_event);
void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx);
void setupTime();
bool isTimeValid(struct tm *timeinfo);
void checkSchedule();
void checkFactoryReset();
void loadSettings();
void saveSetting(const char* key, int value);
void saveSetting(const char* key, bool value);
void resetDevice();

// -----------------------------------------------------------------------------
// Main Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000); // Allow serial to stabilize
  Serial.println("\n--- Starting Compressor Controller ---");

  // Configure Pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Default Off (Safety)

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Load Settings from NVS
  loadSettings();

  // ---------------------------------------------------------------------------
  // RainMaker Initialization
  // ---------------------------------------------------------------------------
  Node my_node;
  my_node = RMaker.initNode(NODE_NAME);

  // Initialize Switch Device with Callback
  my_switch.addCb(write_callback);

  // Note: Standard 'Power' param is added automatically by Switch constructor.

  // Add Custom Parameters for Scheduling
  // 1. Enable Schedule
  Param param_sched_en("Enable Schedule", RMakerVal(schedule_enabled), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_sched_en.addUIType(RMAKER_UI_TOGGLE);
  my_switch.addParam(param_sched_en);

  // 2. Start Time (Hour)
  Param param_start_hr("Start Hour", RMakerVal(start_hr), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_start_hr.addBounds(RMakerVal(0), RMakerVal(23), RMakerVal(1));
  param_start_hr.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_start_hr);

  // 3. Start Time (Minute)
  Param param_start_min("Start Minute", RMakerVal(start_min), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_start_min.addBounds(RMakerVal(0), RMakerVal(59), RMakerVal(5));
  param_start_min.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_start_min);

  // 4. End Time (Hour)
  Param param_end_hr("End Hour", RMakerVal(end_hr), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_end_hr.addBounds(RMakerVal(0), RMakerVal(23), RMakerVal(1));
  param_end_hr.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_end_hr);

  // 5. End Time (Minute)
  Param param_end_min("End Minute", RMakerVal(end_min), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_end_min.addBounds(RMakerVal(0), RMakerVal(59), RMakerVal(5));
  param_end_min.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_end_min);

  // 6. Timezone Offset (Hours from UTC)
  Param param_tz("Timezone Offset", RMakerVal(timezone_offset), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_tz.addBounds(RMakerVal(-12), RMakerVal(14), RMakerVal(1));
  param_tz.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_tz);

  // Add Device to Node
  my_node.addDevice(my_switch);

  // Enable Features
  RMaker.enableOTA(OTA_USING_PARAMS);
  RMaker.enableTZService();
  RMaker.enableSchedule();

  // Start RainMaker
  Serial.println("Starting RainMaker...");
  RMaker.start();

  // Setup WiFi Event Listener
  WiFi.onEvent(sysProvEvent);

  // Initial Time Setup
  setupTime();
}

// -----------------------------------------------------------------------------
// Main Loop
// -----------------------------------------------------------------------------
void loop() {
  // Check for Factory Reset Button Press
  checkFactoryReset();

  // Check Schedule Periodically
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) { // Check every 5 seconds
    lastCheck = millis();
    checkSchedule();
  }

  // Small delay to prevent watchdog starvation
  delay(100);
}

// -----------------------------------------------------------------------------
// Logic: Schedule & Safety
// -----------------------------------------------------------------------------
void checkSchedule() {
  struct tm timeinfo;

  // 1. Safety: Get Local Time. If fails, turn OFF.
  if (!getLocalTime(&timeinfo)) {
    // Only spam serial if we were previously ON or valid
    static bool logged_fail = false;
    if (!logged_fail) {
      Serial.println("SAFETY WARNING: Failed to obtain time. Ensuring Relay is OFF.");
      logged_fail = true;
    }

    if (relay_state == true) {
      relay_state = false;
      digitalWrite(RELAY_PIN, LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
    return;
  }

  // 2. Safety: Validate Time (Year > 2022). If invalid, turn OFF.
  if (!isTimeValid(&timeinfo)) {
    static bool logged_invalid = false;
    if (!logged_invalid) {
      Serial.printf("SAFETY WARNING: Time invalid (Year: %d). Ensuring Relay is OFF.\n", timeinfo.tm_year + 1900);
      logged_invalid = true;
    }

    if (relay_state == true) {
      relay_state = false;
      digitalWrite(RELAY_PIN, LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
    return;
  }

  // 3. Apply Schedule (if enabled)
  if (schedule_enabled) {
    int current_hr = timeinfo.tm_hour;
    int current_min = timeinfo.tm_min;

    // Normalize to minutes from midnight
    int now_mins = current_hr * 60 + current_min;
    int start_mins = start_hr * 60 + start_min;
    int end_mins = end_hr * 60 + end_min;

    bool should_be_on = false;

    if (start_mins < end_mins) {
      // Example: 08:00 to 17:00
      if (now_mins >= start_mins && now_mins < end_mins) {
        should_be_on = true;
      }
    } else if (start_mins > end_mins) {
      // Example: 22:00 to 06:00 (Overnight)
      if (now_mins >= start_mins || now_mins < end_mins) {
        should_be_on = true;
      }
    } else {
      // Start == End. Assume OFF.
      should_be_on = false;
    }

    // Apply only if changed
    if (relay_state != should_be_on) {
      Serial.printf("Schedule Update: Switching %s (Time: %02d:%02d)\n",
                    should_be_on ? "ON" : "OFF", current_hr, current_min);
      relay_state = should_be_on;
      digitalWrite(RELAY_PIN, relay_state ? HIGH : LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
  }
}

bool isTimeValid(struct tm *timeinfo) {
  // Check if year is valid (NTP synced)
  return (timeinfo->tm_year + 1900) > 2022;
}

void setupTime() {
  // Configure time with offset
  // daylightOffset is 0 because we handle it via simple slider offset if needed,
  // or user adjusts the offset.
  configTime(timezone_offset * 3600, 0, ntpServer);
}

// -----------------------------------------------------------------------------
// Logic: Factory Reset
// -----------------------------------------------------------------------------
void checkFactoryReset() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    unsigned long start_press = millis();
    while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      if (millis() - start_press > FACTORY_RESET_TIME_MS) {
        Serial.println("\nFactory Reset Triggered!");
        resetDevice();
        break;
      }
      delay(100);
    }
  }
}

void resetDevice() {
  Serial.println("Clearing NVS and resetting...");

  // Clear Preferences
  prefs.begin("compressor", false);
  prefs.clear();
  prefs.end();

  // Factory Reset RainMaker (clears Wi-Fi and Node ID)
  RMaker.factoryReset(2);
}

// -----------------------------------------------------------------------------
// RainMaker Callbacks
// -----------------------------------------------------------------------------
void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx) {
  const char *device_name = device->getDeviceName();
  const char *param_name = param->getParamName();

  Serial.printf("Received value = %s for %s - %s\n", val.val.b? "true" : "false", device_name, param_name);

  if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
    relay_state = val.val.b;
    digitalWrite(RELAY_PIN, relay_state ? HIGH : LOW);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "Enable Schedule") == 0) {
    schedule_enabled = val.val.b;
    saveSetting("sched_en", schedule_enabled);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "Start Hour") == 0) {
    start_hr = val.val.i;
    saveSetting("start_hr", start_hr);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "Start Minute") == 0) {
    start_min = val.val.i;
    saveSetting("start_min", start_min);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "End Hour") == 0) {
    end_hr = val.val.i;
    saveSetting("end_hr", end_hr);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "End Minute") == 0) {
    end_min = val.val.i;
    saveSetting("end_min", end_min);
    param->updateAndReport(val);
  }
  else if (strcmp(param_name, "Timezone Offset") == 0) {
    timezone_offset = val.val.i;
    saveSetting("tz_offset", timezone_offset);
    setupTime(); // Apply new time
    param->updateAndReport(val);
  }
}

// -----------------------------------------------------------------------------
// Persistence Helpers
// -----------------------------------------------------------------------------
void loadSettings() {
  prefs.begin("compressor", true); // Read-only

  schedule_enabled = prefs.getBool("sched_en", schedule_enabled);
  start_hr = prefs.getInt("start_hr", start_hr);
  start_min = prefs.getInt("start_min", start_min);
  end_hr = prefs.getInt("end_hr", end_hr);
  end_min = prefs.getInt("end_min", end_min);
  timezone_offset = prefs.getInt("tz_offset", timezone_offset);

  prefs.end();
  Serial.printf("Settings Loaded: Sched=%s, Start=%02d:%02d, End=%02d:%02d, TZ=%d\n",
                schedule_enabled ? "ON" : "OFF", start_hr, start_min, end_hr, end_min, timezone_offset);
}

void saveSetting(const char* key, int value) {
  prefs.begin("compressor", false);
  prefs.putInt(key, value);
  prefs.end();
}

void saveSetting(const char* key, bool value) {
  prefs.begin("compressor", false);
  prefs.putBool(key, value);
  prefs.end();
}

// -----------------------------------------------------------------------------
// System Events
// -----------------------------------------------------------------------------
void sysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
#if CONFIG_IDF_TARGET_ESP32
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on BLE\n",
                    (const char *)sys_event->event_info.prov_start.prov_name,
                    (const char *)sys_event->event_info.prov_start.prov_pop);
#else
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on SoftAP\n",
                    (const char *)sys_event->event_info.prov_start.prov_name,
                    (const char *)sys_event->event_info.prov_start.prov_pop);
#endif
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("\nWi-Fi Connected.");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Wi-Fi Got IP: %s\n", WiFi.localIP().toString().c_str());
      setupTime();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("\nWi-Fi Disconnected!");
      // If disconnected, should we turn off safely?
      // checkSchedule will fail to get time eventually if drift happens or reboot happens,
      // but loop continues.
      break;
    default:;
  }
}
