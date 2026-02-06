#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include "time.h"
#include <Preferences.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
// Seeed XIAO ESP32C3 D0 is GPIO 2
#define RELAY_PIN    2

// Device Names
#define DEVICE_NAME  "Compressor"
#define NODE_NAME    "CompressorNode"

// Default Schedule
#define DEFAULT_START_HR  8
#define DEFAULT_START_MIN 0
#define DEFAULT_END_HR    17
#define DEFAULT_END_MIN   0
#define DEFAULT_TZ_OFFSET 0 // UTC by default

// NTP Server
const char* ntpServer = "pool.ntp.org";

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

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
void sysProvEvent(arduino_event_t *sys_event);
void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx);
void setupTime();
bool isTimeValid(struct tm *timeinfo);
void checkSchedule();
void loadSettings();
void saveSetting(const char* key, int value);
void saveSetting(const char* key, bool value);

// -----------------------------------------------------------------------------
// Main Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Configure Relay Pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Default Off

  // Load Settings from NVS
  loadSettings();

  // ---------------------------------------------------------------------------
  // RainMaker Initialization
  // ---------------------------------------------------------------------------
  Node my_node;
  my_node = RMaker.initNode(NODE_NAME);

  // Initialize Switch Device
  my_switch.addCb(write_callback);

  // Add Standard Power Parameter (Already added by Switch constructor, but we ensure it's there)

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
  param_start_min.addBounds(RMakerVal(0), RMakerVal(59), RMakerVal(5)); // Step 5 for easier UI
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
  // Simple integer slider for -12 to +14
  Param param_tz("Timezone Offset", RMakerVal(timezone_offset), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  param_tz.addBounds(RMakerVal(-12), RMakerVal(14), RMakerVal(1));
  param_tz.addUIType(RMAKER_UI_SLIDER);
  my_switch.addParam(param_tz);

  // Add Device to Node
  my_node.addDevice(my_switch);

  // Event Handling (Provisioning, Wi-Fi, etc.)
  RMaker.enableOTA(OTA_USING_PARAMS);
  RMaker.enableTZService(); // Enables standard Timezone service
  RMaker.enableSchedule();  // Enables RainMaker Cloud Schedules (optional backup)

  // Start RainMaker
  Serial.printf("\nStarting RainMaker...");
  RMaker.start();

  // Setup WiFi Event Listener for Provisioning info
  WiFi.onEvent(sysProvEvent);

  // Setup NTP
  setupTime();
}

// -----------------------------------------------------------------------------
// Main Loop
// -----------------------------------------------------------------------------
void loop() {
  // Check the schedule every loop (or you could throttle this to once per second)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) { // Check every 5 seconds
    lastCheck = millis();
    checkSchedule();
  }

  // Allow RainMaker to work
  delay(100);
}

// -----------------------------------------------------------------------------
// Schedule Logic
// -----------------------------------------------------------------------------
void checkSchedule() {
  struct tm timeinfo;

  // 1. Get Local Time
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    // SAFETY: If we don't know the time, and we rely on schedule, what to do?
    // User requested: "compressor is to be off if the esp does not know time"
    if (relay_state == true) {
      Serial.println("SAFETY: Time invalid, turning OFF.");
      relay_state = false;
      digitalWrite(RELAY_PIN, LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
    return;
  }

  // 2. Validate Time (Check if year is reasonable, e.g. > 2020)
  if (!isTimeValid(&timeinfo)) {
    Serial.println("Time not synced yet (Year < 2022). Keeping OFF.");
     if (relay_state == true) {
      relay_state = false;
      digitalWrite(RELAY_PIN, LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
    return;
  }

  // 3. Check Schedule
  if (schedule_enabled) {
    int current_hr = timeinfo.tm_hour;
    int current_min = timeinfo.tm_min;

    // Convert everything to minutes for easy comparison
    int now_mins = current_hr * 60 + current_min;
    int start_mins = start_hr * 60 + start_min;
    int end_mins = end_hr * 60 + end_min;

    bool should_be_on = false;

    if (start_mins < end_mins) {
      // Normal day schedule (e.g. 08:00 to 17:00)
      if (now_mins >= start_mins && now_mins < end_mins) {
        should_be_on = true;
      }
    } else if (start_mins > end_mins) {
      // Overnight schedule (e.g. 22:00 to 06:00)
      if (now_mins >= start_mins || now_mins < end_mins) {
        should_be_on = true;
      }
    } else {
      // Start == End? Assume OFF or Always ON? Let's assume OFF for safety if equal.
      should_be_on = false;
    }

    // Apply State
    if (relay_state != should_be_on) {
      Serial.printf("Schedule Update: Switching %s\n", should_be_on ? "ON" : "OFF");
      relay_state = should_be_on;
      digitalWrite(RELAY_PIN, relay_state ? HIGH : LOW);
      my_switch.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, relay_state);
    }
  }
}

bool isTimeValid(struct tm *timeinfo) {
  // If year is close to 1970, NTP hasn't worked.
  return (timeinfo->tm_year + 1900) > 2022;
}

void setupTime() {
  // Configure Timezone.
  // Note: RainMaker has its own TZ service, but we also manually set it here for the local logic
  // "gmtOffset_sec" logic is simple, but we can update it from the param.
  // Actually, we should apply the timezone offset from the param dynamically.
  // For now, init with default.
  configTime(timezone_offset * 3600, 0, ntpServer);
}

// -----------------------------------------------------------------------------
// RainMaker Callbacks
// -----------------------------------------------------------------------------

// Callback for writing parameters from App
void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx) {
  const char *device_name = device->getDeviceName();
  const char *param_name = param->getParamName();

  Serial.printf("Received value = %s for %s - %s\n", val.val.b? "true" : "false", device_name, param_name);

  if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
    // If user manually toggles power
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
    // Re-config time
    configTime(timezone_offset * 3600, 0, ntpServer);
    param->updateAndReport(val);
  }
}

// -----------------------------------------------------------------------------
// Persistence Helpers
// -----------------------------------------------------------------------------
void loadSettings() {
  Preferences prefs;
  prefs.begin("compressor", true); // Read-only mode

  schedule_enabled = prefs.getBool("sched_en", schedule_enabled);
  start_hr = prefs.getInt("start_hr", start_hr);
  start_min = prefs.getInt("start_min", start_min);
  end_hr = prefs.getInt("end_hr", end_hr);
  end_min = prefs.getInt("end_min", end_min);
  timezone_offset = prefs.getInt("tz_offset", timezone_offset);

  prefs.end();
  Serial.printf("Settings Loaded: Sched=%d, Start=%02d:%02d, End=%02d:%02d, TZ=%d\n",
                schedule_enabled, start_hr, start_min, end_hr, end_min, timezone_offset);
}

void saveSetting(const char* key, int value) {
  Preferences prefs;
  prefs.begin("compressor", false); // Read-write mode
  prefs.putInt(key, value);
  prefs.end();
}

void saveSetting(const char* key, bool value) {
  Preferences prefs;
  prefs.begin("compressor", false); // Read-write mode
  prefs.putBool(key, value);
  prefs.end();
}

// Event handler for Provisioning and System events
void sysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
#if CONFIG_IDF_TARGET_ESP32
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on BLE\n",
                    (const char *)sys_event->event_info.prov_start.prov_name,
                    (const char *)sys_event->event_info.prov_start.prov_pop);
      // printQR(sys_event->event_info.prov_start.prov_name, sys_event->event_info.prov_start.prov_pop, "ble");
#else
      Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on SoftAP\n",
                    (const char *)sys_event->event_info.prov_start.prov_name,
                    (const char *)sys_event->event_info.prov_start.prov_pop);
      // printQR(sys_event->event_info.prov_start.prov_name, sys_event->event_info.prov_start.prov_pop, "softap");
#endif
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.printf("\nConnected to Wi-Fi!\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Got IP: %s\n", WiFi.localIP().toString().c_str());
      // Re-trigger time sync just in case
      setupTime();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("\nDisconnected from Wi-Fi!\n");
      break;
    default:;
  }
}
