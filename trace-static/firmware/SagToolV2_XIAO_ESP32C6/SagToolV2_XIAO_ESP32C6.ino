#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ============================================================
// OPTIONAL HARDWARE
// ============================================================

#define USE_BUZZER true
#define USE_NEOPIXEL true
#include "SagToolV2StatusPixel.h"

constexpr char FW_VERSION[] = "2.0.10";

// ============================================================
// PIN CONFIGURATION
// ============================================================

// XIAO ESP32-C6 pad names followed by the corresponding GPIO numbers.
constexpr uint8_t PIN_BATTERY = 0;  // D0, midpoint of 100 kΩ / 100 kΩ divider
constexpr uint8_t PIN_ZERO    = 1;  // D1
constexpr uint8_t PIN_MODE    = 2;  // D2
constexpr uint8_t PIN_BUZZER  = 21; // D3, active buzzer trigger
constexpr uint8_t PIN_SDA     = 22; // D4
constexpr uint8_t PIN_SCL     = 23; // D5
// NeoPixel output is D10 / GPIO18 in SagToolV2StatusPixel.h.

// ============================================================
// WIFI
// ============================================================

const char *AP_SSID     = "SagTool";
const char *AP_PASSWORD = "sagtool123";

WebServer server(80);

String savedSSID;
String savedPassword;

bool staCredentialsPresent = false;
bool staConnecting = false;

uint32_t staConnectStartedMs = 0;
uint32_t lastReconnectAttemptMs = 0;

constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t STA_RETRY_INTERVAL_MS  = 30000;

bool scanRunning = false;

// ============================================================
// OLED
// ============================================================

constexpr uint8_t OLED_ADDR = 0x3C;

constexpr int OLED_WIDTH  = 128;
constexpr int OLED_HEIGHT = 64;

constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint32_t OLED_REFRESH_MS = 100;
constexpr size_t OLED_FRAME_BYTES = OLED_WIDTH * OLED_HEIGHT / 8;
constexpr size_t OLED_CHUNK_BYTES = 31;
size_t oledBytesSent = OLED_FRAME_BYTES;

Adafruit_SSD1306 display(
  OLED_WIDTH,
  OLED_HEIGHT,
  &Wire,
  -1,
  I2C_CLOCK_HZ,
  I2C_CLOCK_HZ
);

bool oledOK = false;

// ============================================================
// NEOPIXEL
// ============================================================

SagToolV2::StatusPixel statusPixel;

// ============================================================
// MT6701
// ============================================================

constexpr uint8_t MT6701_ADDR      = 0x06;
constexpr uint8_t MT6701_ANGLE_MSB = 0x03;
constexpr uint8_t MT6701_ANGLE_LSB = 0x04;

constexpr int32_t COUNTS_PER_REV = 16384;
constexpr int32_t HALF_REV       = 8192;

uint16_t rawAngle = 0;
uint16_t previousAngle = 0;

int32_t revolutionCount = 0;
int64_t continuousCounts = 0;

bool encoderOK = false;
bool trackingLost = false;
bool haveEncoderSample = false;
uint32_t lastEncoderUs = 0;
uint32_t maxEncoderGapUs = 0;
uint32_t encoderReadErrors = 0;
// A single-turn sensor cannot resolve direction past half a turn between samples.
// Treat gaps over 50 ms as suspect even when the next read succeeds.
constexpr uint32_t MAX_ENCODER_GAP_US = 50000;

// ============================================================
// CALIBRATION
// ============================================================

constexpr float DEFAULT_SPOOL_DIAMETER_MM = 20.0f;

constexpr float DEFAULT_MM_PER_COUNT =
    (PI * DEFAULT_SPOOL_DIAMETER_MM)
    / COUNTS_PER_REV;

float mmPerCount = DEFAULT_MM_PER_COUNT;

bool calibrationValid = false;

int64_t calibrationHomeCounts = 0;

// ============================================================
// OPTIONAL SUSPENSION TRAVEL
// ============================================================

float suspensionTravelMm = 0.0f;

// ============================================================
// PREFERENCES
// ============================================================

Preferences prefs;

// ============================================================
// MODES / STATES
// ============================================================

enum MeasureMode {
  MODE_RIDER,
  MODE_STATIC,
  MODE_LIVE
};

enum MeasureState {
  STATE_READY,
  STATE_ARMED,
  STATE_MEASURING,
  STATE_CAPTURED
};

MeasureMode mode = MODE_RIDER;
MeasureState state = STATE_READY;

// ============================================================
// MEASUREMENT
// ============================================================

int64_t zeroCounts = 0;

float liveDisplacementMm = 0.0f;
float capturedSagMm = 0.0f;

// ============================================================
// AUTO CAPTURE
// ============================================================

constexpr float LOAD_DETECT_MM = 4.0f;
constexpr float MIN_CAPTURE_MM = 5.0f;

constexpr float STABILITY_WINDOW_MM = 0.8f;
constexpr uint32_t STABILITY_TIME_MS = 1500;

float stabilityMinMm = 0.0f;
float stabilityMaxMm = 0.0f;

uint32_t stabilityStartMs = 0;

// ============================================================
// BUTTONS
// ============================================================

constexpr uint32_t DEBOUNCE_MS = 35;

bool lastZeroReading = HIGH;
bool lastModeReading = HIGH;

bool zeroStableState = HIGH;
bool modeStableState = HIGH;

uint32_t zeroDebounceMs = 0;
uint32_t modeDebounceMs = 0;

// ============================================================
// TIMING
// ============================================================

uint32_t lastDisplayMs = 0;
uint32_t lastSerialMs = 0;

// ============================================================
// NAME HELPERS
// ============================================================

const char *modeName() {

  switch (mode) {

    case MODE_RIDER:
      return "RIDER";

    case MODE_STATIC:
      return "STATIC";

    case MODE_LIVE:
      return "LIVE";
  }

  return "?";
}

const char *stateName() {

  switch (state) {

    case STATE_READY:
      return "READY";

    case STATE_ARMED:
      return "ARMED";

    case STATE_MEASURING:
      return "MEASURING";

    case STATE_CAPTURED:
      return "CAPTURED";
  }

  return "?";
}

// ============================================================
// BUZZER
// ============================================================

#if USE_BUZZER
constexpr bool BUZZER_ACTIVE_HIGH = true;
constexpr bool BUZZER_USE_PWM = true;
constexpr uint32_t BUZZER_PWM_HZ = 4000;
bool buzzerPwmReady = false;
uint32_t buzzerStartedMs = 0;
uint16_t buzzerDurationMs = 0;
enum class BuzzerAlert : uint8_t { None, LowBattery, Inactivity };
// Declare after the enum so Arduino's prototype generator cannot place it earlier.
void startBuzzerAlert(BuzzerAlert alert, uint8_t pulseCount, uint32_t now);
BuzzerAlert buzzerAlert = BuzzerAlert::None;
uint8_t alertPulsesRemaining = 0;
uint32_t alertLastPulseMs = 0;

void writeBuzzer(bool on) {
  if (buzzerPwmReady) {
    ledcWrite(PIN_BUZZER, on ? 128 : 0);  // 50% duty at 8-bit resolution.
  } else {
    digitalWrite(PIN_BUZZER, on == BUZZER_ACTIVE_HIGH ? HIGH : LOW);
  }
}

void cancelBuzzerAlert() {
  if (buzzerAlert == BuzzerAlert::None) return;
  buzzerAlert = BuzzerAlert::None;
  buzzerDurationMs = 0;
  writeBuzzer(false);
}
#endif

void buzzerBegin() {
#if USE_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
  writeBuzzer(false);
  if (BUZZER_USE_PWM && ledcAttach(PIN_BUZZER, BUZZER_PWM_HZ, 8)) {
    buzzerPwmReady = ledcOutputInvert(PIN_BUZZER, !BUZZER_ACTIVE_HIGH);
    if (!buzzerPwmReady) {
      ledcDetach(PIN_BUZZER);
      pinMode(PIN_BUZZER, OUTPUT);
    }
    writeBuzzer(false);
  }
#endif
}

void startBuzzer(uint16_t durationMs) {
#if USE_BUZZER
  buzzerStartedMs = millis();
  buzzerDurationMs = durationMs;
  writeBuzzer(true);
#else
  (void)durationMs;
#endif
}

void updateBuzzer() {
#if USE_BUZZER
  if (buzzerDurationMs != 0 && millis() - buzzerStartedMs >= buzzerDurationMs) {
    writeBuzzer(false);
    buzzerDurationMs = 0;
  }
#endif
}

void soundZero() {
#if USE_BUZZER
  cancelBuzzerAlert();
#endif
  startBuzzer(70);
}

void soundCapture() {
#if USE_BUZZER
  cancelBuzzerAlert();
#endif
  startBuzzer(140);
}

void soundMode() {
#if USE_BUZZER
  cancelBuzzerAlert();
#endif
  startBuzzer(200);
}

// ============================================================
// NEOPIXEL
// ============================================================

void updatePixel() {
  SagToolV2::Mode pixelMode = SagToolV2::Mode::Rider;
  if (mode == MODE_STATIC) pixelMode = SagToolV2::Mode::Static;
  if (mode == MODE_LIVE) pixelMode = SagToolV2::Mode::Live;

  SagToolV2::State pixelState = SagToolV2::State::Ready;
  switch (state) {
    case STATE_READY: break;
    case STATE_ARMED: pixelState = SagToolV2::State::Armed; break;
    case STATE_MEASURING: pixelState = SagToolV2::State::Measuring; break;
    case STATE_CAPTURED: pixelState = SagToolV2::State::Captured; break;
  }
  statusPixel.update(pixelMode, pixelState, encoderOK && !trackingLost, millis());
}

// ============================================================
// BATTERY MONITOR
// ============================================================

constexpr float BATTERY_DIVIDER_GAIN = 2.0f;
constexpr uint32_t BATTERY_UPDATE_MS = 1000;
constexpr uint8_t BATTERY_SAMPLES = 16;
float batteryVolts = 0.0f;
int batteryPercent = -1;
uint32_t lastBatteryMs = 0;

#if USE_BUZZER
constexpr int LOW_BATTERY_PERCENT = 5;
constexpr int LOW_BATTERY_REARM_PERCENT = 8;
constexpr uint32_t INACTIVITY_MS = 20UL * 60 * 1000;
constexpr uint32_t LOW_BATTERY_REPEAT_MS = 60UL * 1000;
constexpr uint32_t INACTIVITY_REPEAT_MS = 5UL * 60 * 1000;
constexpr uint32_t ALERT_PULSE_INTERVAL_MS = 250;
constexpr uint16_t ALERT_BEEP_MS = 100;
constexpr float ACTIVITY_MOVEMENT_MM = 1.0f;
uint32_t lastActivityMs = 0;
int64_t activityCounts = 0;
bool lowBatteryLatched = false;
bool lowBatteryAcknowledged = false;
bool lowBatteryAlerted = false;
bool inactivityAlerted = false;
uint32_t lastLowBatteryAlertMs = 0;
uint32_t lastInactivityAlertMs = 0;

void startBuzzerAlert(BuzzerAlert alert, uint8_t pulseCount, uint32_t now) {
  buzzerAlert = alert;
  alertPulsesRemaining = pulseCount - 1;
  alertLastPulseMs = now;
  startBuzzer(ALERT_BEEP_MS);
}
#endif

void markActivity(uint32_t now, bool acknowledgeBattery = false) {
#if USE_BUZZER
  lastActivityMs = now;
  activityCounts = continuousCounts;
  inactivityAlerted = false;
  if (acknowledgeBattery && lowBatteryLatched) lowBatteryAcknowledged = true;
  if (buzzerAlert == BuzzerAlert::Inactivity ||
      (buzzerAlert == BuzzerAlert::LowBattery && lowBatteryAcknowledged)) {
    cancelBuzzerAlert();
  }
#else
  (void)now;
  (void)acknowledgeBattery;
#endif
}

void updateBuzzerAlerts(uint32_t now) {
#if USE_BUZZER
  if (encoderOK && !trackingLost &&
      fabs(static_cast<double>(continuousCounts - activityCounts)) * mmPerCount >=
          ACTIVITY_MOVEMENT_MM) {
    markActivity(now);
  }

  if (batteryPercent >= LOW_BATTERY_REARM_PERCENT) {
    lowBatteryLatched = false;
    lowBatteryAcknowledged = false;
    lowBatteryAlerted = false;
  } else if (batteryPercent >= 0 && batteryPercent <= LOW_BATTERY_PERCENT) {
    lowBatteryLatched = true;
  }
  const bool lowBatteryDue = lowBatteryLatched && !lowBatteryAcknowledged && batteryPercent >= 0;
  if (buzzerAlert == BuzzerAlert::LowBattery && !lowBatteryDue) cancelBuzzerAlert();
  if (buzzerDurationMs != 0) return;

  if (buzzerAlert != BuzzerAlert::None) {
    if (alertPulsesRemaining == 0) {
      buzzerAlert = BuzzerAlert::None;
    } else if (now - alertLastPulseMs >= ALERT_PULSE_INTERVAL_MS) {
      --alertPulsesRemaining;
      alertLastPulseMs = now;
      startBuzzer(ALERT_BEEP_MS);
    }
    return;
  }

  if (lowBatteryDue &&
      (!lowBatteryAlerted || now - lastLowBatteryAlertMs >= LOW_BATTERY_REPEAT_MS)) {
    lowBatteryAlerted = true;
    lastLowBatteryAlertMs = now;
    startBuzzerAlert(BuzzerAlert::LowBattery, 3, now);
  } else if (now - lastActivityMs >= INACTIVITY_MS &&
             (!inactivityAlerted || now - lastInactivityAlertMs >= INACTIVITY_REPEAT_MS)) {
    inactivityAlerted = true;
    lastInactivityAlertMs = now;
    startBuzzerAlert(BuzzerAlert::Inactivity, 2, now);
  }
#else
  (void)now;
#endif
}

void sampleBattery() {
  uint32_t totalMillivolts = 0;
  for (uint8_t sample = 0; sample < BATTERY_SAMPLES; ++sample) {
    totalMillivolts += analogReadMilliVolts(PIN_BATTERY);
  }
  batteryVolts = totalMillivolts * BATTERY_DIVIDER_GAIN / (1000.0f * BATTERY_SAMPLES);

  // Approximate 1S Li-ion charge level from terminal voltage.
  // A disconnected sense switch typically reads close to zero.
  if (batteryVolts < 2.0f) {
    batteryPercent = -1;
    return;
  }
  constexpr float volts[] = {3.20f, 3.50f, 3.65f, 3.75f, 3.85f, 4.00f, 4.20f};
  constexpr int percent[] = {0, 10, 25, 45, 65, 85, 100};
  batteryPercent = 0;
  for (size_t i = 1; i < sizeof(volts) / sizeof(volts[0]); ++i) {
    if (batteryVolts < volts[i]) {
      const float fraction = constrain((batteryVolts - volts[i - 1]) /
                                       (volts[i] - volts[i - 1]), 0.0f, 1.0f);
      batteryPercent = lroundf(percent[i - 1] + fraction * (percent[i] - percent[i - 1]));
      return;
    }
  }
  batteryPercent = 100;
}

// ============================================================
// MT6701 I2C
// ============================================================

bool readRegister(
  uint8_t reg,
  uint8_t &value
) {

  Wire.beginTransmission(
    MT6701_ADDR
  );

  Wire.write(reg);

  if (
    Wire.endTransmission(false)
    != 0
  ) {

    return false;
  }

  if (
    Wire.requestFrom(
      MT6701_ADDR,
      (uint8_t)1
    ) != 1
  ) {

    return false;
  }

  value = Wire.read();

  return true;
}

bool readMT6701(
  uint16_t &angle
) {

  uint8_t msb;
  uint8_t lsb;

  if (
    !readRegister(
      MT6701_ANGLE_MSB,
      msb
    )
  ) {

    return false;
  }

  if (
    !readRegister(
      MT6701_ANGLE_LSB,
      lsb
    )
  ) {

    return false;
  }

  angle =
      ((uint16_t)msb << 6)
      |
      ((uint16_t)lsb >> 2);

  angle &= 0x3FFF;

  return true;
}

// ============================================================
// MULTI-TURN TRACKING
// ============================================================

void updateEncoder(
  uint16_t newAngle
) {

  int32_t delta =
      (int32_t)newAngle
      -
      (int32_t)previousAngle;

  // 16383 -> 0
  if (
    delta < -HALF_REV
  ) {

    revolutionCount++;
  }

  // 0 -> 16383
  else if (
    delta > HALF_REV
  ) {

    revolutionCount--;
  }

  rawAngle =
      newAngle;

  continuousCounts =
      ((int64_t)revolutionCount
       * COUNTS_PER_REV)
      +
      rawAngle;

  previousAngle =
      newAngle;
}

// ============================================================
// SETTINGS / NVS
// ============================================================

void loadSettings() {

  prefs.begin(
    "sagtool",
    true
  );

  calibrationValid =
      prefs.getBool(
        "calvalid",
        false
      );

  mmPerCount =
      prefs.getFloat(
        "mmcount",
        DEFAULT_MM_PER_COUNT
      );

  suspensionTravelMm =
      prefs.getFloat(
        "travel",
        0.0f
      );

  savedSSID =
      prefs.getString(
        "wifi_ssid",
        ""
      );

  savedPassword =
      prefs.getString(
        "wifi_pass",
        ""
      );

  prefs.end();

  staCredentialsPresent =
      savedSSID.length()
      >
      0;
}

void saveCalibration() {

  prefs.begin(
    "sagtool",
    false
  );

  prefs.putBool(
    "calvalid",
    true
  );

  prefs.putFloat(
    "mmcount",
    mmPerCount
  );

  prefs.end();

  calibrationValid =
      true;
}

void saveTravel() {

  prefs.begin(
    "sagtool",
    false
  );

  prefs.putFloat(
    "travel",
    suspensionTravelMm
  );

  prefs.end();
}

void saveWiFiCredentials(
  const String &ssid,
  const String &password
) {

  prefs.begin(
    "sagtool",
    false
  );

  prefs.putString(
    "wifi_ssid",
    ssid
  );

  prefs.putString(
    "wifi_pass",
    password
  );

  prefs.end();

  savedSSID =
      ssid;

  savedPassword =
      password;

  staCredentialsPresent =
      savedSSID.length()
      >
      0;
}

void forgetWiFiCredentials() {

  prefs.begin(
    "sagtool",
    false
  );

  prefs.remove(
    "wifi_ssid"
  );

  prefs.remove(
    "wifi_pass"
  );

  prefs.end();

  savedSSID =
      "";

  savedPassword =
      "";

  staCredentialsPresent =
      false;
}

// ============================================================
// DISTANCE
// ============================================================

float calculateDisplacementMm() {

  int64_t difference =
      continuousCounts
      -
      zeroCounts;

  return
      fabs((double)difference)
      *
      mmPerCount;
}

float resultMm() {

  if (
    state == STATE_CAPTURED
  ) {

    return capturedSagMm;
  }

  return liveDisplacementMm;
}

float resultPercent() {

  if (
    suspensionTravelMm
    <=
    0.0f
  ) {

    return 0.0f;
  }

  return
      resultMm()
      /
      suspensionTravelMm
      *
      100.0f;
}

// ============================================================
// ZERO / ARM
// ============================================================

void zeroAndArm() {
  markActivity(millis(), true);
  if (!encoderOK) {
    Serial.println("ZERO ignored: encoder read unavailable.");
    return;
  }
  trackingLost = false;
  maxEncoderGapUs = 0;
  encoderReadErrors = 0;

  zeroCounts =
      continuousCounts;

  calibrationHomeCounts =
      continuousCounts;

  liveDisplacementMm =
      0.0f;

  capturedSagMm =
      0.0f;

  stabilityStartMs =
      0;

  stabilityMinMm =
      0.0f;

  stabilityMaxMm =
      0.0f;

  if (
    mode == MODE_LIVE
  ) {

    state =
        STATE_READY;
  }

  else {

    state =
        STATE_ARMED;
  }

  Serial.println();
  Serial.println("ZERO SET");

  if (
    mode != MODE_LIVE
  ) {

    Serial.println(
      "AUTO CAPTURE ARMED"
    );
  }

  soundZero();

}

// ============================================================
// CAPTURE ENGINE
// ============================================================

void updateMeasurement() {

  liveDisplacementMm =
      calculateDisplacementMm();

  if (
    mode == MODE_LIVE
  ) {

    return;
  }

  switch (state) {

    // --------------------------------------------------------

    case STATE_READY:

      break;

    // --------------------------------------------------------

    case STATE_ARMED:

      if (
        liveDisplacementMm
        >=
        LOAD_DETECT_MM
      ) {

        state =
            STATE_MEASURING;

        stabilityStartMs =
            millis();

        stabilityMinMm =
            liveDisplacementMm;

        stabilityMaxMm =
            liveDisplacementMm;


        Serial.println(
          "Load detected."
        );
      }

      break;

    // --------------------------------------------------------

    case STATE_MEASURING: {

      // Load disappeared before capture
      if (
        liveDisplacementMm
        <
        LOAD_DETECT_MM
      ) {

        state =
            STATE_ARMED;

        stabilityStartMs =
            0;


        Serial.println(
          "Load removed. Re-armed."
        );

        break;
      }

      if (
        liveDisplacementMm
        <
        stabilityMinMm
      ) {

        stabilityMinMm =
            liveDisplacementMm;
      }

      if (
        liveDisplacementMm
        >
        stabilityMaxMm
      ) {

        stabilityMaxMm =
            liveDisplacementMm;
      }

      float span =
          stabilityMaxMm
          -
          stabilityMinMm;

      // Too much movement; restart stable window
      if (
        span
        >
        STABILITY_WINDOW_MM
      ) {

        stabilityStartMs =
            millis();

        stabilityMinMm =
            liveDisplacementMm;

        stabilityMaxMm =
            liveDisplacementMm;
      }

      else if (
        millis()
        -
        stabilityStartMs
        >=
        STABILITY_TIME_MS
      ) {

        if (
          liveDisplacementMm
          >=
          MIN_CAPTURE_MM
        ) {

          capturedSagMm =
              (
                stabilityMinMm
                +
                stabilityMaxMm
              )
              *
              0.5f;

          state =
              STATE_CAPTURED;

          Serial.println();
          Serial.println(
            "*** CAPTURED ***"
          );

          Serial.printf(
            "%.2f mm\n",
            capturedSagMm
          );

          if (
            suspensionTravelMm
            >
            0.0f
          ) {

            Serial.printf(
              "%.1f %%\n",
              resultPercent()
            );
          }

          soundCapture();

        }
      }

      break;
    }

    // --------------------------------------------------------

    case STATE_CAPTURED:

      // Hold captured value until re-zeroed
      break;
  }
}

// ============================================================
// MODE
// ============================================================

void nextMode() {
  markActivity(millis(), true);

  if (
    mode == MODE_RIDER
  ) {

    mode =
        MODE_STATIC;
  }

  else if (
    mode == MODE_STATIC
  ) {

    mode =
        MODE_LIVE;
  }

  else {

    mode =
        MODE_RIDER;
  }

  state =
      STATE_READY;

  capturedSagMm =
      0.0f;

  soundMode();


  Serial.print(
    "Mode: "
  );

  Serial.println(
    modeName()
  );
}

void setMode(
  MeasureMode newMode
) {
  markActivity(millis(), true);

  mode =
      newMode;

  state =
      STATE_READY;

  capturedSagMm =
      0.0f;

  soundMode();
}

// ============================================================
// BUTTONS
// ============================================================

void updateButtons() {

  // ----------------------------------------------------------
  // ZERO BUTTON
  // ----------------------------------------------------------

  bool reading =
      digitalRead(
        PIN_ZERO
      );

  if (
    reading
    !=
    lastZeroReading
  ) {

    zeroDebounceMs =
        millis();

    lastZeroReading =
        reading;
  }

  if (
    millis()
    -
    zeroDebounceMs
    >
    DEBOUNCE_MS
  ) {

    if (
      reading
      !=
      zeroStableState
    ) {

      zeroStableState =
          reading;

      if (
        zeroStableState
        ==
        LOW
      ) {

        zeroAndArm();
      }
    }
  }

  // ----------------------------------------------------------
  // MODE BUTTON
  // ----------------------------------------------------------

  reading =
      digitalRead(
        PIN_MODE
      );

  if (
    reading
    !=
    lastModeReading
  ) {

    modeDebounceMs =
        millis();

    lastModeReading =
        reading;
  }

  if (
    millis()
    -
    modeDebounceMs
    >
    DEBOUNCE_MS
  ) {

    if (
      reading
      !=
      modeStableState
    ) {

      modeStableState =
          reading;

      if (
        modeStableState
        ==
        LOW
      ) {

        nextMode();
      }
    }
  }
}

// ============================================================
// WIFI HELPERS
// ============================================================

bool staConnected() {

  return
      WiFi.status()
      ==
      WL_CONNECTED;
}

String staIPAddress() {

  if (
    !staConnected()
  ) {

    return "";
  }

  return
      WiFi.localIP()
      .toString();
}

// ============================================================
// WIFI CONNECTION MANAGEMENT
// ============================================================

void beginSTAConnection() {

  if (
    !staCredentialsPresent
  ) {

    return;
  }

  Serial.print(
    "Connecting STA to: "
  );

  Serial.println(
    savedSSID
  );

  WiFi.begin(
    savedSSID.c_str(),
    savedPassword.length()
      ?
      savedPassword.c_str()
      :
      nullptr
  );

  staConnecting =
      true;

  staConnectStartedMs =
      millis();

  lastReconnectAttemptMs =
      millis();
}

void updateSTAConnection() {

  if (
    staConnected()
  ) {

    staConnecting =
        false;

    return;
  }

  if (
    !staCredentialsPresent
  ) {

    staConnecting =
        false;

    return;
  }

  uint32_t now =
      millis();

  if (
    staConnecting
  ) {

    if (
      now
      -
      staConnectStartedMs
      >=
      STA_CONNECT_TIMEOUT_MS
    ) {

      staConnecting =
          false;

      Serial.println(
        "STA connection timeout."
      );
    }

    return;
  }

  if (
    now
    -
    lastReconnectAttemptMs
    >=
    STA_RETRY_INTERVAL_MS
  ) {

    Serial.println(
      "Retrying saved WiFi..."
    );

    beginSTAConnection();
  }
}

// ============================================================
// WIFI SCAN
// ============================================================

void startAsyncScan() {

  if (
    scanRunning
  ) {

    return;
  }

  WiFi.scanDelete();

  int result =
      WiFi.scanNetworks(
        true,
        false
      );

  if (
    result
    ==
    WIFI_SCAN_RUNNING
  ) {

    scanRunning =
        true;
  }
}

String scanResultsJSON() {

  int count =
      WiFi.scanComplete();

  if (
    count
    ==
    WIFI_SCAN_RUNNING
  ) {

    return
      "{\"running\":true,\"networks\":[]}";
  }

  if (
    count
    <
    0
  ) {

    scanRunning =
        false;

    return
      "{\"running\":false,\"networks\":[]}";
  }

  String json =
      "{\"running\":false,\"networks\":[";

  for (
    int i = 0;
    i < count;
    i++
  ) {

    if (
      i > 0
    ) {

      json += ",";
    }

    String ssid =
        WiFi.SSID(i);

    ssid.replace(
      "\\",
      "\\\\"
    );

    ssid.replace(
      "\"",
      "\\\""
    );

    json += "{";

    json += "\"ssid\":\"";
    json += ssid;
    json += "\",";

    json += "\"rssi\":";
    json += String(
      WiFi.RSSI(i)
    );

    json += "}";
  }

  json += "]}";

  scanRunning =
      false;

  return json;
}

// ============================================================
// OLED
// ============================================================

void drawScreen() {

  if (
    !oledOK
  ) {

    return;
  }

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(
    1
  );

  // ----------------------------------------------------------
  // HEADER
  // ----------------------------------------------------------

  display.setCursor(
    0,
    0
  );

  display.print(
    modeName()
  );

  display.setCursor(
    86,
    0
  );

  if (
    !encoderOK || trackingLost
  ) {

    display.print(
      trackingLost && encoderOK ? "REZERO" : "ERR"
    );
  }

  else if (
    !calibrationValid
  ) {

    display.print(
      "UNCAL"
    );
  }

  else if (
    staConnected()
  ) {

    display.print(
      "HOME"
    );
  }

  else {

    display.print(
      "AP"
    );
  }

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  // ----------------------------------------------------------
  // MAIN SAG VALUE
  // ----------------------------------------------------------

  display.setCursor(
    0,
    15
  );

  display.setTextSize(
    2
  );

  if (trackingLost) {
    display.print("--");
  } else {
    display.print(resultMm(), 1);
  }

  display.setTextSize(
    1
  );

  display.print(
    " mm"
  );

  // ----------------------------------------------------------
  // PERCENT
  // ----------------------------------------------------------

  if (
    !trackingLost && suspensionTravelMm > 0.0f
  ) {

    display.setCursor(
      88,
      17
    );

    display.print(
      resultPercent(),
      1
    );

    display.print(
      "%"
    );
  }

  // ----------------------------------------------------------
  // STATE
  // ----------------------------------------------------------

  display.setCursor(0, 32);
  if (staConnected()) {
    display.print(WiFi.localIP());
  } else {
    display.print("AP 192.168.4.1");
  }

  display.setCursor(
    0,
    41
  );

  if (trackingLost) {
    display.print("RETURN HOME, ZERO");
  } else switch (state) {


    case STATE_READY:

      if (
        mode == MODE_LIVE
      ) {

        display.print(
          "LIVE MEASUREMENT"
        );
      }

      else {

        display.print(
          "PRESS ZERO"
        );
      }

      break;

    case STATE_ARMED:

      display.print(
        "ARMED - LOAD BIKE"
      );

      break;

    case STATE_MEASURING:

      display.print(
        "STABILIZING..."
      );

      break;

    case STATE_CAPTURED:

      display.print(
        "CAPTURED"
      );

      break;
  }

  // ----------------------------------------------------------
  // BOTTOM LINE
  // ----------------------------------------------------------

  display.setCursor(
    0,
    54
  );

  display.print(FW_VERSION);
  if (batteryPercent >= 0) {
    // The default OLED font advances six pixels per character.
    const int percentChars = batteryPercent == 100 ? 4 : (batteryPercent >= 10 ? 3 : 2);
    constexpr int voltageX = OLED_WIDTH - 5 * 6;  // "4.20V"
    display.setCursor(voltageX - 2 - percentChars * 6, 54);
    display.print(batteryPercent);
    display.print('%');
    display.setCursor(voltageX, 54);
    display.print(batteryVolts, 2);
    display.print('V');
  } else {
    display.setCursor(OLED_WIDTH - 3 * 6, 54);
    display.print("--%");
  }

  oledBytesSent = 0;
}

void sendOledChunk() {
  if (!oledOK || oledBytesSent == OLED_FRAME_BYTES) return;

  if (oledBytesSent == 0) {
    // SSD1306 horizontal mode is set by display.begin().
    const uint8_t addressWindow[] = {
      0x00, SSD1306_COLUMNADDR, 0, OLED_WIDTH - 1,
      SSD1306_PAGEADDR, 0, OLED_HEIGHT / 8 - 1
    };
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(addressWindow, sizeof(addressWindow));
    if (Wire.endTransmission() != 0) {
      oledBytesSent = OLED_FRAME_BYTES;
      return;
    }
  }

  const size_t remaining = OLED_FRAME_BYTES - oledBytesSent;
  const size_t chunk = remaining < OLED_CHUNK_BYTES ? remaining : OLED_CHUNK_BYTES;
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(static_cast<uint8_t>(0x40));  // Display data follows.
  Wire.write(display.getBuffer() + oledBytesSent, chunk);
  if (Wire.endTransmission() == 0) {
    oledBytesSent += chunk;
  } else {
    oledBytesSent = OLED_FRAME_BYTES;
  }
}

// ============================================================
// CALIBRATION
// ============================================================

void calibrate(
  float knownDistanceMm
) {
  if (trackingLost || !encoderOK) {
    Serial.println("Calibration ignored: return home and ZERO with encoder connected.");
    return;
  }

  int64_t difference =
      continuousCounts
      -
      calibrationHomeCounts;

  if (
    difference == 0
    ||
    knownDistanceMm <= 0
  ) {

    Serial.println(
      "Calibration failed."
    );

    return;
  }

  mmPerCount =
      knownDistanceMm
      /
      fabs((double)difference);

  saveCalibration();

  liveDisplacementMm =
      calculateDisplacementMm();

  Serial.println();
  Serial.println(
    "CALIBRATION COMPLETE"
  );

  Serial.printf(
    "Distance: %.3f mm\n",
    knownDistanceMm
  );

  Serial.printf(
    "Counts: %lld\n",
    difference
  );

  Serial.printf(
    "mm/count: %.8f\n",
    mmPerCount
  );

  float circumference =
      mmPerCount
      *
      COUNTS_PER_REV;

  Serial.printf(
    "Effective diameter: %.3f mm\n",
    circumference / PI
  );
}

// ============================================================
// SERIAL COMMANDS
// ============================================================

void processSerial() {

  static String pendingCommand;
  static bool tooLong = false;
  if (!Serial.available()) return;
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\n') break;
    if (pendingCommand.length() < 32) {
      pendingCommand += ch;
    } else {
      tooLong = true;
    }
    if (!Serial.available()) return;
  }
  if (tooLong) {
    pendingCommand = "";
    tooLong = false;
    Serial.println("Serial command too long.");
    return;
  }
  if (pendingCommand.isEmpty()) return;
  String command = pendingCommand;
  pendingCommand = "";

  command.trim();

  if (
    command == "h"
    ||
    command == "H"
  ) {

    zeroAndArm();

    return;
  }

  if (
    command.startsWith("c")
    ||
    command.startsWith("C")
  ) {

    String value =
        command.substring(1);

    value.trim();

    calibrate(
      value.toFloat()
    );

    return;
  }

  if (
    command == "m"
    ||
    command == "M"
  ) {

    nextMode();

    return;
  }

  Serial.println(
    "Commands: h | c200 | m"
  );
}

// ============================================================
// MAIN SAG PAGE
// ============================================================

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>Sag Tool</title>

<style>

:root {
  color-scheme: dark;
}

body {
  margin:0;
  background:#111;
  color:#eee;
  font-family:Arial,Helvetica,sans-serif;
}

.container {
  max-width:520px;
  margin:auto;
  padding:22px;
}

h1 {
  text-align:center;
  letter-spacing:2px;
  margin-bottom:5px;
}

.sub {
  text-align:center;
  color:#888;
  margin-bottom:24px;
}

.card {
  background:#1d1d1d;
  border-radius:14px;
  padding:18px;
  margin-bottom:16px;
}

.value {
  font-size:64px;
  font-weight:700;
  text-align:center;
  line-height:1;
}

.units {
  font-size:22px;
  color:#aaa;
}

.percent {
  font-size:30px;
  text-align:center;
  margin-top:10px;
}

.state {
  text-align:center;
  font-size:18px;
  margin-top:14px;
  letter-spacing:1px;
}

.row {
  display:flex;
  gap:10px;
}

button {
  flex:1;
  border:none;
  border-radius:10px;
  padding:15px;
  font-size:16px;
  font-weight:bold;
  cursor:pointer;
}

.primary {
  background:#efefef;
  color:#111;
}

.secondary {
  background:#333;
  color:#eee;
}

label {
  display:block;
  color:#aaa;
  margin-bottom:8px;
}

input {
  width:100%;
  box-sizing:border-box;
  background:#111;
  color:white;
  border:1px solid #555;
  border-radius:8px;
  padding:12px;
  font-size:18px;
}

.small {
  color:#999;
  font-size:13px;
  margin-top:8px;
}

.battery-summary {
  display:flex;
  flex-wrap:wrap;
  align-items:center;
  justify-content:center;
  gap:6px 10px;
  margin:-8px auto 16px;
  padding:8px 14px;
  width:fit-content;
  max-width:100%;
  box-sizing:border-box;
  border-radius:10px;
  background:#1d1d1d;
  color:#aaa;
  font-size:14px;
}

.battery-summary strong {
  color:#eee;
}

.firmware-version {
  text-align:center;
  color:#aaa;
  font-size:13px;
  margin:8px 0;
}

.statusbar {
  display:flex;
  flex-wrap:wrap;
  gap:8px 12px;
  justify-content:space-between;
  align-items:center;
  font-size:13px;
  color:#999;
}

.good {
  color:#8dff8d;
}

.bad {
  color:#ff7777;
}

.footer {
  text-align:center;
  margin-top:26px;
  font-size:14px;
}

.footer a {
  color:#999;
  text-decoration:none;
}

.footer a:hover {
  color:#fff;
}

</style>

</head>

<body>

<div class="container">

<h1>SAG TOOL</h1>

<div class="sub">
Cascade Singletrack
</div>

<div class="battery-summary">
<span>Battery</span>
<strong id="battery">--</strong>
</div>

<div class="card">

  <div class="value">

    <span id="sag">
      0.0
    </span>

    <span class="units">
      mm
    </span>

  </div>

  <div
    id="percent"
    class="percent">
  </div>

  <div
    id="state"
    class="state">

    READY

  </div>

</div>

<div class="row">

  <button
    class="primary"
    onclick="zeroTool()">

    ZERO / ARM

  </button>

  <button
    class="secondary"
    onclick="cycleMode()">

    MODE

  </button>

</div>

<br>

<div class="card">

<label>
Measurement Mode
</label>

<div class="row">

<button
class="secondary"
onclick="setMode('rider')">

RIDER

</button>

<button
class="secondary"
onclick="setMode('static')">

STATIC

</button>

<button
class="secondary"
onclick="setMode('live')">

LIVE

</button>

</div>

<div class="small">

Current mode:

<strong id="mode">
RIDER
</strong>

</div>

</div>

<div class="card">

<label>
Suspension travel
</label>

<input
id="travel"
type="number"
min="0"
max="300"
step="0.5"
placeholder="Optional &mdash; example: 170">

<br><br>

<button
class="secondary"
onclick="saveTravel()">

SAVE TRAVEL

</button>

<div class="small">
Leave at 0 for millimeter-only measurement.
</div>

</div>

<div class="card">

<div class="statusbar">

<span>
Encoder:
<strong id="encoder">
--
</strong>
</span>

<span>
Calibration:
<strong id="calibration">
--
</strong>
</span>

<span>
Wi-Fi:
<strong id="wifiState">
--
</strong>
</span>

</div>

</div>

<div class="footer">

<a href="/wifi">
Wi-Fi Settings
</a>

<div class="firmware-version">Firmware <span id="firmware">--</span></div>

</div>

</div>

<script>

let travelLoaded = false;

async function getStatus() {

  try {

    const response =
      await fetch('/status');

    const d =
      await response.json();

    document
      .getElementById('sag')
      .textContent =
      d.trackingLost ? '--' : d.value.toFixed(1);

    document
      .getElementById('state')
      .textContent =
      d.state;

    document
      .getElementById('mode')
      .textContent =
      d.mode;

    document.getElementById('battery').textContent =
      d.batteryPercent === null ? '--' :
      d.batteryPercent + '% / ' + d.batteryVoltage.toFixed(2) + ' V';
    document.getElementById('firmware').textContent = d.firmware;

    const enc =
      document
        .getElementById('encoder');

    enc.textContent =
      d.encoder
      ? 'OK'
      : (d.trackingLost ? 'RETURN HOME / ZERO' : 'ERROR');

    enc.className =
      d.encoder
      ? 'good'
      : 'bad';

    const cal =
      document
        .getElementById('calibration');

    cal.textContent =
      d.calibrated
      ? 'OK'
      : 'NOT SET';

    cal.className =
      d.calibrated
      ? 'good'
      : 'bad';

    const wifi =
      document
        .getElementById('wifiState');

    if (
      d.staConnected
    ) {

      wifi.textContent =
        'HOME';

      wifi.className =
        'good';
    }

    else {

      wifi.textContent =
        'AP';

      wifi.className =
        '';
    }

    if (
      !d.trackingLost && d.travel > 0
    ) {

      document
        .getElementById('percent')
        .textContent =
        d.percent.toFixed(1)
        +
        '%';
    }

    else {

      document
        .getElementById('percent')
        .textContent =
        '';
    }

    if (
      !travelLoaded
    ) {

      document
        .getElementById('travel')
        .value =
        d.travel > 0
        ? d.travel
        : '';

      travelLoaded =
        true;
    }
  }

  catch (e) {

    document
      .getElementById('state')
      .textContent =
      'CONNECTION LOST';
  }
}

async function zeroTool() {

  await fetch(
    '/zero'
  );

  getStatus();
}

async function cycleMode() {

  await fetch(
    '/mode'
  );

  getStatus();
}

async function setMode(mode) {

  await fetch(
    '/setmode?mode='
    +
    encodeURIComponent(mode)
  );

  getStatus();
}

async function saveTravel() {

  let value =
    document
      .getElementById('travel')
      .value;

  if (
    value === ''
  ) {

    value = 0;
  }

  await fetch(
    '/travel?mm='
    +
    encodeURIComponent(value)
  );

  travelLoaded =
    false;

  getStatus();
}

setInterval(
  getStatus,
  250
);

getStatus();

</script>

</body>

</html>
)rawliteral";

// ============================================================
// WIFI SETTINGS PAGE
// ============================================================

const char WIFI_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>Sag Tool Wi-Fi</title>

<style>

:root {
  color-scheme: dark;
}

body {
  margin:0;
  background:#111;
  color:#eee;
  font-family:Arial,Helvetica,sans-serif;
}

.container {
  max-width:520px;
  margin:auto;
  padding:22px;
}

h1 {
  text-align:center;
  letter-spacing:2px;
  margin-bottom:5px;
}

.sub {
  text-align:center;
  color:#888;
  margin-bottom:24px;
}

.card {
  background:#1d1d1d;
  border-radius:14px;
  padding:18px;
  margin-bottom:16px;
}

.statusgrid {
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:8px;
  font-size:14px;
}

button {
  width:100%;
  border:none;
  border-radius:10px;
  padding:15px;
  font-size:16px;
  font-weight:bold;
  cursor:pointer;
}

.primary {
  background:#efefef;
  color:#111;
}

.secondary {
  background:#333;
  color:#eee;
}

.danger {
  background:#542020;
  color:#fff;
}

label {
  display:block;
  color:#aaa;
  margin-bottom:8px;
}

input {
  width:100%;
  box-sizing:border-box;
  background:#111;
  color:white;
  border:1px solid #555;
  border-radius:8px;
  padding:12px;
  font-size:18px;
}

.network {
  padding:12px;
  margin-top:8px;
  background:#292929;
  border-radius:8px;
  cursor:pointer;
}

.network:hover {
  background:#373737;
}

.small {
  color:#999;
  font-size:13px;
  margin-top:8px;
}

.good {
  color:#8dff8d;
}

.dim {
  color:#aaa;
}

.back {
  text-align:center;
  margin-top:26px;
}

.back a {
  color:#aaa;
  text-decoration:none;
  font-size:15px;
}

</style>

</head>

<body>

<div class="container">

<h1>WI-FI</h1>

<div class="sub">
Sag Tool Connectivity
</div>

<div class="card">

<label>
SagTool Access Point
</label>

<div class="statusgrid">

<div>
SSID:
</div>

<div>
SagTool
</div>

<div>
IP:
</div>

<div>
192.168.4.1
</div>

<div>
Clients:
</div>

<div id="clients">
0
</div>

</div>

<div class="small">
This network always remains available.
</div>

</div>

<div class="card">

<label>
External Wi-Fi
</label>

<div class="statusgrid">

<div>
Status:
</div>

<div id="staStatus">
--
</div>

<div>
SSID:
</div>

<div id="staSsid">
--
</div>

<div>
IP:
</div>

<div id="staIp">
--
</div>

<div>
Signal:
</div>

<div id="staRssi">
--
</div>

</div>

</div>

<div class="card">

<button
class="secondary"
onclick="startScan()">

SCAN NETWORKS

</button>

<div id="networks">
</div>

</div>

<div class="card">

<label>
SSID
</label>

<input
id="wifiSsid"
type="text">

<br>

<label>
Password
</label>

<input
id="wifiPass"
type="password">

<br>

<button
class="primary"
onclick="connectWifi()">

SAVE & CONNECT

</button>

</div>

<div class="card">

<button
class="danger"
onclick="forgetWifi()">

FORGET EXTERNAL WI-FI

</button>

</div>

<div class="back">

<a href="/">
Back to Sag Tool
</a>

</div>

</div>

<script>

let scanPoll = null;

async function updateWifiStatus() {

  try {

    const response =
      await fetch('/status');

    const d =
      await response.json();

    document
      .getElementById('clients')
      .textContent =
      d.clients;

    const sta =
      document
        .getElementById('staStatus');

    sta.textContent =
      d.staConnected
      ? 'Connected'
      :
      (
        d.staConnecting
        ? 'Connecting...'
        : 'Not connected'
      );

    sta.className =
      d.staConnected
      ? 'good'
      : 'dim';

    document
      .getElementById('staSsid')
      .textContent =
      d.staSsid || '--';

    document
      .getElementById('staIp')
      .textContent =
      d.staIp || '--';

    document
      .getElementById('staRssi')
      .textContent =
      d.staConnected
      ? d.staRssi + ' dBm'
      : '--';
  }

  catch (e) {

    document
      .getElementById('staStatus')
      .textContent =
      'Connection lost';
  }
}

async function startScan() {

  document
    .getElementById('networks')
    .innerHTML =
    '<div class="small">Scanning...</div>';

  await fetch(
    '/wifi/scan/start'
  );

  if (
    scanPoll
  ) {

    clearInterval(
      scanPoll
    );
  }

  scanPoll =
    setInterval(
      checkScan,
      750
    );
}

async function checkScan() {

  const r =
    await fetch(
      '/wifi/scan/results'
    );

  const d =
    await r.json();

  if (
    d.running
  ) {

    return;
  }

  clearInterval(
    scanPoll
  );

  scanPoll =
    null;

  const box =
    document
      .getElementById(
        'networks'
      );

  box.innerHTML =
    '';

  if (
    d.networks.length
    ===
    0
  ) {

    box.innerHTML =
      '<div class="small">No networks found.</div>';

    return;
  }

  d.networks.forEach(
    n => {

      const div =
        document
          .createElement(
            'div'
          );

      div.className =
        'network';

      div.textContent =
        n.ssid
        +
        '   '
        +
        n.rssi
        +
        ' dBm';

      div.onclick =
        () => {

          document
            .getElementById(
              'wifiSsid'
            )
            .value =
            n.ssid;
        };

      box.appendChild(
        div
      );
    }
  );
}

async function connectWifi() {

  const ssid =
    document
      .getElementById(
        'wifiSsid'
      )
      .value;

  const pass =
    document
      .getElementById(
        'wifiPass'
      )
      .value;

  if (
    !ssid
  ) {

    alert(
      'Enter or select an SSID.'
    );

    return;
  }

  const body =
    new URLSearchParams();

  body.append(
    'ssid',
    ssid
  );

  body.append(
    'pass',
    pass
  );

  await fetch(
    '/wifi/connect',
    {

      method:
        'POST',

      headers: {
        'Content-Type':
          'application/x-www-form-urlencoded'
      },

      body:
        body.toString()
    }
  );

  updateWifiStatus();
}

async function forgetWifi() {

  await fetch(
    '/wifi/forget',
    {
      method:
        'POST'
    }
  );

  document
    .getElementById(
      'wifiPass'
    )
    .value =
    '';

  document
    .getElementById(
      'wifiSsid'
    )
    .value =
    '';

  updateWifiStatus();
}

setInterval(
  updateWifiStatus,
  1000
);

updateWifiStatus();

</script>

</body>

</html>
)rawliteral";

// ============================================================
// STATUS JSON
// ============================================================

String makeStatusJSON() {

  String json =
      "{";

  json += "\"firmware\":\"";
  json += FW_VERSION;
  json += "\",\"batteryVoltage\":";
  json += batteryPercent >= 0 ? String(batteryVolts, 3) : String("null");
  json += ",\"batteryPercent\":";
  json += batteryPercent >= 0 ? String(batteryPercent) : String("null");
  json += ",";

  json +=
      "\"mode\":\"";

  json +=
      modeName();

  json +=
      "\",";

  json +=
      "\"state\":\"";

  json +=
      stateName();

  json +=
      "\",";

  json +=
      "\"value\":";

  json +=
      String(
        resultMm(),
        3
      );

  json +=
      ",";

  json +=
      "\"raw\":";

  json +=
      String(
        liveDisplacementMm,
        3
      );

  json +=
      ",";

  json +=
      "\"travel\":";

  json +=
      String(
        suspensionTravelMm,
        2
      );

  json +=
      ",";

  json +=
      "\"percent\":";

  json +=
      String(
        resultPercent(),
        3
      );

  json +=
      ",";

  json +=
      "\"encoder\":";

  json += encoderOK && !trackingLost ? "true" : "false";
  json += ",\"trackingLost\":";
  json += trackingLost ? "true" : "false";
  json += ",\"encoderMaxGapUs\":";
  json += String(maxEncoderGapUs);
  json += ",\"encoderReadErrors\":";
  json += String(encoderReadErrors);

  json +=
      ",";

  json +=
      "\"calibrated\":";

  json +=
      calibrationValid
      ?
      "true"
      :
      "false";

  json +=
      ",";

  json +=
      "\"clients\":";

  json +=
      String(
        WiFi.softAPgetStationNum()
      );

  json +=
      ",";

  json +=
      "\"staConnected\":";

  json +=
      staConnected()
      ?
      "true"
      :
      "false";

  json +=
      ",";

  json +=
      "\"staConnecting\":";

  json +=
      staConnecting
      ?
      "true"
      :
      "false";

  json +=
      ",";

  json +=
      "\"staSsid\":\"";

  String safeSSID =
      savedSSID;

  safeSSID.replace(
    "\\",
    "\\\\"
  );

  safeSSID.replace(
    "\"",
    "\\\""
  );

  json +=
      safeSSID;

  json +=
      "\",";

  json +=
      "\"staIp\":\"";

  json +=
      staIPAddress();

  json +=
      "\",";

  json +=
      "\"staRssi\":";

  if (
    staConnected()
  ) {

    json +=
        String(
          WiFi.RSSI()
        );
  }

  else {

    json +=
        "0";
  }

  json +=
      "}";

  return json;
}

// ============================================================
// WEB SERVER
// ============================================================

void setupWebServer() {

  // ----------------------------------------------------------
  // MAIN SAG PAGE
  // ----------------------------------------------------------

  server.on(
    "/",
    HTTP_GET,
    []() {

      server.send_P(
        200,
        "text/html",
        PAGE_HTML
      );
    }
  );

  // ----------------------------------------------------------
  // WIFI SETTINGS PAGE
  // ----------------------------------------------------------

  server.on(
    "/wifi",
    HTTP_GET,
    []() {

      server.send_P(
        200,
        "text/html",
        WIFI_PAGE_HTML
      );
    }
  );

  // ----------------------------------------------------------
  // STATUS
  // ----------------------------------------------------------

  server.on(
    "/status",
    HTTP_GET,
    []() {

      server.sendHeader(
        "Cache-Control",
        "no-store"
      );

      server.send(
        200,
        "application/json",
        makeStatusJSON()
      );
    }
  );

  // ----------------------------------------------------------
  // ZERO
  // ----------------------------------------------------------

  server.on(
    "/zero",
    HTTP_GET,
    []() {

      zeroAndArm();

      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );

  // ----------------------------------------------------------
  // CYCLE MODE
  // ----------------------------------------------------------

  server.on(
    "/mode",
    HTTP_GET,
    []() {

      nextMode();

      server.send(
        200,
        "text/plain",
        modeName()
      );
    }
  );

  // ----------------------------------------------------------
  // SET SPECIFIC MODE
  // ----------------------------------------------------------

  server.on(
    "/setmode",
    HTTP_GET,
    []() {

      if (
        !server.hasArg(
          "mode"
        )
      ) {

        server.send(
          400,
          "text/plain",
          "Missing mode"
        );

        return;
      }

      String requested =
          server.arg(
            "mode"
          );

      requested.toLowerCase();

      if (
        requested
        ==
        "rider"
      ) {

        setMode(
          MODE_RIDER
        );
      }

      else if (
        requested
        ==
        "static"
      ) {

        setMode(
          MODE_STATIC
        );
      }

      else if (
        requested
        ==
        "live"
      ) {

        setMode(
          MODE_LIVE
        );
      }

      else {

        server.send(
          400,
          "text/plain",
          "Invalid mode"
        );

        return;
      }

      server.send(
        200,
        "text/plain",
        modeName()
      );
    }
  );

  // ----------------------------------------------------------
  // TRAVEL
  // ----------------------------------------------------------

  server.on(
    "/travel",
    HTTP_GET,
    []() {

      if (
        !server.hasArg(
          "mm"
        )
      ) {

        server.send(
          400,
          "text/plain",
          "Missing mm"
        );

        return;
      }

      float newTravel =
          server.arg(
            "mm"
          )
          .toFloat();

      if (
        newTravel < 0.0f
        ||
        newTravel > 300.0f
      ) {

        server.send(
          400,
          "text/plain",
          "Invalid travel"
        );

        return;
      }

      suspensionTravelMm =
          newTravel;

      saveTravel();

      Serial.printf(
        "Travel set: %.1f mm\n",
        suspensionTravelMm
      );

      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );

  // ----------------------------------------------------------
  // WIFI SCAN START
  // ----------------------------------------------------------

  server.on(
    "/wifi/scan/start",
    HTTP_GET,
    []() {

      startAsyncScan();

      server.send(
        200,
        "application/json",
        "{\"started\":true}"
      );
    }
  );

  // ----------------------------------------------------------
  // WIFI SCAN RESULTS
  // ----------------------------------------------------------

  server.on(
    "/wifi/scan/results",
    HTTP_GET,
    []() {

      server.send(
        200,
        "application/json",
        scanResultsJSON()
      );
    }
  );

  // ----------------------------------------------------------
  // WIFI CONNECT
  // ----------------------------------------------------------

  server.on(
    "/wifi/connect",
    HTTP_POST,
    []() {

      if (
        !server.hasArg(
          "ssid"
        )
      ) {

        server.send(
          400,
          "text/plain",
          "Missing SSID"
        );

        return;
      }

      String ssid =
          server.arg(
            "ssid"
          );

      String password =
          server.hasArg(
            "pass"
          )
          ?
          server.arg(
            "pass"
          )
          :
          "";

      ssid.trim();

      if (
        ssid.length()
        ==
        0
      ) {

        server.send(
          400,
          "text/plain",
          "Invalid SSID"
        );

        return;
      }

      saveWiFiCredentials(
        ssid,
        password
      );

      WiFi.disconnect(
        false,
        false
      );

      delay(
        50
      );

      beginSTAConnection();

      server.send(
        200,
        "text/plain",
        "CONNECTING"
      );
    }
  );

  // ----------------------------------------------------------
  // WIFI FORGET
  // ----------------------------------------------------------

  server.on(
    "/wifi/forget",
    HTTP_POST,
    []() {

      forgetWiFiCredentials();

      WiFi.disconnect(
        false,
        false
      );

      staConnecting =
          false;

      server.send(
        200,
        "text/plain",
        "FORGOTTEN"
      );
    }
  );

  // ----------------------------------------------------------
  // 404
  // ----------------------------------------------------------

  server.onNotFound(
    []() {

      server.send(
        404,
        "text/plain",
        "Not found"
      );
    }
  );

  server.begin();

  Serial.println(
    "Web server started."
  );
}

// ============================================================
// WIFI STARTUP
// ============================================================

void startWiFi() {

  WiFi.mode(
    WIFI_AP_STA
  );

  WiFi.setAutoReconnect(
    true
  );

  bool apOK =
      WiFi.softAP(
        AP_SSID,
        AP_PASSWORD
      );

  Serial.println();

  Serial.print(
    "SagTool AP: "
  );

  Serial.println(
    apOK
    ?
    "STARTED"
    :
    "FAILED"
  );

  if (
    apOK
  ) {

    Serial.print(
      "AP IP: "
    );

    Serial.println(
      WiFi.softAPIP()
    );
  }

  if (
    staCredentialsPresent
  ) {

    beginSTAConnection();
  }

  else {

    Serial.println(
      "No external WiFi saved."
    );
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  buzzerBegin();
  statusPixel.begin();

  Serial.begin(
    115200
  );

  delay(
    400
  );

  Serial.println();
  Serial.println(
    "========================="
  );
  Serial.printf(" SAG TOOL V2 FW %s\n", FW_VERSION);
  Serial.println(
    "========================="
  );

  // Buttons
  pinMode(
    PIN_ZERO,
    INPUT_PULLUP
  );

  pinMode(
    PIN_MODE,
    INPUT_PULLUP
  );

  // Battery monitor
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  sampleBattery();
  lastBatteryMs = millis();

  // I2C
  Wire.begin(
    PIN_SDA,
    PIN_SCL
  );

  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(10);

  // OLED
  oledOK =
      display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR,
        true,
        false  // Wire already configured on D4/D5.
      );

  if (oledOK) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SAG TOOL V2");
    display.println(FW_VERSION);
    display.display();
  }

  // Persistent settings
  loadSettings();

  // Wi-Fi
  startWiFi();

  // HTTP
  setupWebServer();

  // Encoder
  encoderOK =
      readMT6701(
        rawAngle
      );

  if (encoderOK) {
    haveEncoderSample = true;
    lastEncoderUs = micros();

    previousAngle =
        rawAngle;

    continuousCounts =
        rawAngle;

    zeroCounts =
        continuousCounts;

    calibrationHomeCounts =
        continuousCounts;
  }

  // Diagnostics
  Serial.printf(
    "Encoder: %s\n",
    encoderOK
      ?
      "PASS"
      :
      "FAIL"
  );

  Serial.printf(
    "OLED: %s\n",
    oledOK
      ?
      "PASS"
      :
      "FAIL"
  );

  Serial.printf(
    "Calibration: %s\n",
    calibrationValid
      ?
      "STORED"
      :
      "NOT SET"
  );

  Serial.printf(
    "Travel: %.1f mm\n",
    suspensionTravelMm
  );

  Serial.println();
  Serial.println(
    "SagTool AP:"
  );

  Serial.println(
    "http://192.168.4.1"
  );


  updatePixel();
  drawScreen();
  markActivity(millis());
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  updateBuzzer();

  // ----------------------------------------------------------
  // ENCODER
  // ----------------------------------------------------------

  uint16_t newAngle;
  encoderOK = readMT6701(newAngle);
  if (encoderOK) {
    const uint32_t sampleUs = micros();
    if (haveEncoderSample) {
      const uint32_t gapUs = sampleUs - lastEncoderUs;
      if (gapUs > maxEncoderGapUs) maxEncoderGapUs = gapUs;
      const int32_t rawDelta = static_cast<int32_t>(newAngle) - previousAngle;
      if (gapUs > MAX_ENCODER_GAP_US || abs(rawDelta) == HALF_REV) {
        if (!trackingLost) {
          Serial.printf("Tracking lost: gap %lu us, angle delta %ld. Return home and ZERO.\n",
                        static_cast<unsigned long>(gapUs), static_cast<long>(rawDelta));
        }
        trackingLost = true;
        state = STATE_READY;
        stabilityStartMs = 0;
      }
    }
    if (!haveEncoderSample || trackingLost) {
      // Start a new single-turn reference. The old multi-turn position is unknown.
      revolutionCount = 0;
      previousAngle = newAngle;
      rawAngle = newAngle;
      continuousCounts = newAngle;
      if (!haveEncoderSample) {
        zeroCounts = continuousCounts;
        calibrationHomeCounts = continuousCounts;
      }
    }
    lastEncoderUs = sampleUs;
    haveEncoderSample = true;
    if (!trackingLost) {
      updateEncoder(newAngle);
      updateMeasurement();
    }
  } else {
    ++encoderReadErrors;
    if (state == STATE_MEASURING) {
      // Require a fresh stability window after a failed sample.
      state = STATE_ARMED;
      stabilityStartMs = 0;
    }
  }

  // ----------------------------------------------------------
  // LOCAL INPUT
  // ----------------------------------------------------------

  updateButtons();

  // ----------------------------------------------------------
  // SERIAL
  // ----------------------------------------------------------

  processSerial();

  // ----------------------------------------------------------
  // WEB
  // ----------------------------------------------------------

  server.handleClient();

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  updateSTAConnection();

  // ----------------------------------------------------------
  // STATUS AND OLED
  // ----------------------------------------------------------

  const uint32_t now = millis();
  if (now - lastBatteryMs >= BATTERY_UPDATE_MS) {
    lastBatteryMs = now;
    sampleBattery();
  }
  updateBuzzerAlerts(now);
  updatePixel();

  if (oledOK && oledBytesSent == OLED_FRAME_BYTES &&
      now - lastDisplayMs >= OLED_REFRESH_MS) {
    lastDisplayMs = now;
    drawScreen();
  }
  sendOledChunk();

  // ----------------------------------------------------------
  // SERIAL DEBUG
  // ----------------------------------------------------------

  if (
    now
    -
    lastSerialMs
    >=
    1000
  ) {

    lastSerialMs =
        now;

    String currentIP =
        staConnected()
        ?
        WiFi.localIP()
          .toString()
        :
        "-";

    Serial.printf("Encoder gap max: %.2f ms  Read errors: %lu  Tracking: %s  ",
                  maxEncoderGapUs / 1000.0f,
                  static_cast<unsigned long>(encoderReadErrors),
                  trackingLost ? "REZERO" : "OK");
    Serial.printf("Battery: %.2f V (%d%%)  ", batteryVolts, batteryPercent);
    if (encoderOK && !trackingLost) {
      const double turns = static_cast<double>(continuousCounts - zeroCounts) / COUNTS_PER_REV;
      Serial.printf("Turns from zero: %+.3f  mm/turn: %.2f  ", turns, mmPerCount * COUNTS_PER_REV);
    }
    Serial.printf(
      "Mode:%-6s "
      "State:%-10s "
      "Live:%7.2f "
      "Result:%7.2f "
      "Travel:%6.1f "
      "STA:%s "
      "IP:%s\n",

      modeName(),

      stateName(),

      liveDisplacementMm,

      resultMm(),

      suspensionTravelMm,

      staConnected()
        ?
        "CONNECTED"
        :
        (
          staConnecting
          ?
          "CONNECTING"
          :
          "OFFLINE"
        ),

      currentIP.c_str()
    );
  }

  delay(
    2
  );
}
