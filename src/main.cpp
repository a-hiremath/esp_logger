#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
#include <WiFi.h>
#include "PubSubClient.h"

// ==========================================
//               CONFIGURATION
// ==========================================

// --- Network Settings ---
const char* SSID          = "yardnet";
const char* PASS          = "YogiYogi835";
const char* MQTT_HOST     = "10.0.0.21";
const uint16_t MQTT_PORT  = 1883;
const char* DEVICE_ID     = "esp32-01";
const char* TOPIC_EVENTS  = "tracker/esp32-01/events";

// --- Pin Definitions ---
#define PIN_ENC_A    32
#define PIN_ENC_B    33
#define PIN_ENC_SW   25

#define PIN_OLED_MOSI 23
#define PIN_OLED_CLK  18
#define PIN_OLED_DC   2
#define PIN_OLED_CS   5
#define PIN_OLED_RST  4

// --- System Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DISPLAY_FPS 30
#define MQTT_RETRY_INTERVAL 5000 // ms

// ==========================================
//               GLOBALS
// ==========================================

// Objects
Adafruit_SH1106 display(PIN_OLED_MOSI, PIN_OLED_CLK, PIN_OLED_DC, PIN_OLED_RST, PIN_OLED_CS);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// State Machine

enum menuState {
  menu,
  objective,
  subjective
};

menuState currentState = menu;

// Data Structures
struct DataPoint {
  uint32_t timestamp;
  int16_t value;
};

const int MAX_RECORDS = 50;
DataPoint dataLog[MAX_RECORDS];
int logIndex = 0;

// State Variables
int menuSelection = 0;
int subMenuSelection = 0;
volatile int encoderCounter = 0;
volatile int lastCounter;
volatile int lastEncoded = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastButtonPress = 0;

// ==========================================
//           INTERRUPT SERVICE ROUTINES
// ==========================================

void IRAM_ATTR updateEncoder() {
  int MSB = digitalRead(PIN_ENC_A);
  int LSB = digitalRead(PIN_ENC_B);

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCounter++;
  if((sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) && encoderCounter > 0) encoderCounter--;

  lastEncoded = encoded;
}

// ==========================================
//             HELPER FUNCTIONS
// ==========================================

void printCentered(String text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  // Note: This is blocking during setup only, which is acceptable.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());
}

// Non-blocking MQTT Manager
void manageMQTT() {
  // 1. If WiFi is down, do nothing
  if (WiFi.status() != WL_CONNECTED) return;

  // 2. If MQTT is connected, process packets
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  // 3. If NOT connected, check if it's time to retry (Non-blocking)
  unsigned long now = millis();
  if (now - lastMqttRetry > MQTT_RETRY_INTERVAL) {
    lastMqttRetry = now;
    Serial.print("Attempting MQTT connection... ");

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    if (mqtt.connect(DEVICE_ID)) {
      Serial.println("CONNECTED");
    } else {
      Serial.print("FAILED, rc=");
      Serial.println(mqtt.state());
    }
  }
}

void saveData() {
  if (logIndex >= MAX_RECORDS) {
    Serial.println("Memory Full!");
    return;
  }

  // 1. Save to Memory
  dataLog[logIndex].timestamp = millis();
  dataLog[logIndex].value = encoderCounter;

  // 2. Visual Feedback (Flash Screen)
  display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.display();
  // Short blocking delay is okay for feedback, but keep it minimal
  delay(50);

  // 3. Publish to MQTT
  Serial.printf("Saved! [%d] Time: %lu Val: %d\n", logIndex, dataLog[logIndex].timestamp, dataLog[logIndex].value);

  if (mqtt.connected()) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"timestamp\":%lu, \"val\":%d}", millis(), encoderCounter);
    mqtt.publish(TOPIC_EVENTS, payload);
  }

  logIndex++;
}

void handleInput() {
  if (digitalRead(PIN_ENC_SW) == LOW) {
    if (millis() - lastButtonPress > 300) {

      switch (currentState) {

        // --- MAIN MENU SELECTION ---
      case menu:
        if (menuSelection == 0) {
          currentState = objective;
          subMenuSelection = 0; // Reset to top when entering
        } else {
          currentState = subjective;
          // moodValue = 4; // (Reset mood if you implement that later)
        }
        break;

        // --- OBJECTIVE MENU SELECTION ---
      case objective:
        if (subMenuSelection == 0) {
          // Enter Caffeine Tracking Mode (You need to build this state!)
          // currentState = trackCaffeine;
        }
        else if (subMenuSelection == 1) {
          // Enter Melatonin Tracking Mode
          // currentState = trackMelatonin;
        }
        else if (subMenuSelection == 2) {
          // BACK BUTTON CLICKED -> Go Home
          currentState = menu;
        }
        break;
      }
      lastButtonPress = millis();
    }
  }
}

void handleEncoder() {
  // FIX: Divide by 2 to ignore the "half-steps" between clicks
  int currentCounter = encoderCounter / 2;
  int delta = 0;

  delta = currentCounter - lastCounter;

  if (delta != 0) {
    switch (currentState) {

      // --- MAIN MENU (2 Items: 0-1) ---
    case menu:
      if (delta > 0) menuSelection = 1;      // Snap to bottom
      else if (delta < 0) menuSelection = 0; // Snap to top
      break;

      // --- OBJECTIVE MENU (3 Items: 0-1-2) ---
    case objective:
      if (delta > 0) subMenuSelection++;
      else if (delta < 0) subMenuSelection--;

      // Clamp to 0-2 (Caffeine, Melatonin, Back)
      if (subMenuSelection > 2) subMenuSelection = 2;
      if (subMenuSelection < 0) subMenuSelection = 0;
      break;
    }
    lastCounter = currentCounter;
  }
}

void drawMenu() {
  display.clearDisplay();

  // --- 1. Draw The Header ---
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 12, 128, 12, WHITE); // Horizontal divider line

  // --- 2. Draw Option A: Objective ---
  // If selected (0), draw a white box and use black text
  if (menuSelection == 0) {
    display.fillRect(0, 16, 128, 20, WHITE); // Draw the highlight bar
    display.setTextColor(BLACK);             // Invert text color
  } else {
    display.setTextColor(WHITE);             // Normal text color
  }

  display.setCursor(10, 22);
  display.setTextSize(1);
  display.println("OBJECTIVE");

  // --- 3. Draw Option B: Subjective ---
  // If selected (1), draw a white box and use black text
  if (menuSelection == 1) {
    display.fillRect(0, 40, 128, 20, WHITE); // Draw the highlight bar
    display.setTextColor(BLACK);             // Invert text color
  } else {
    display.setTextColor(WHITE);             // Normal text color
  }

  display.setCursor(10, 46);
  display.println("SUBJECTIVE");

  display.display();
}

void drawObjective() {
  // --- 1. Draw The Header ---
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(35, 2);
  display.println("LOG INTAKE");
  display.drawLine(0, 12, 128, 12, WHITE);

  // --- 2. Option A: Caffeine ---
  if (subMenuSelection == 0) {
    display.fillRect(0, 16, 128, 14, WHITE); // Smaller bar height (14px)
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 19);
  display.println("CAFFEINE (mg)");

  // --- 3. Option B: Melatonin ---
  if (subMenuSelection == 1) {
    display.fillRect(0, 32, 128, 14, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 35);
  display.println("MELATONIN (mg)");

  // --- 4. Option C: BACK ---
  if (subMenuSelection == 2) {
    display.fillRect(0, 48, 128, 14, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 51);
  display.println("< BACK");
}

void updateDisplay() {

  display.clearDisplay();

  // Draw MQTT Status Indicator (Small dot in top right)
  if (mqtt.connected()) {
    display.fillCircle(SCREEN_WIDTH - 4, 4, 2, WHITE);
  }

  switch (currentState)
  {
  case menu:
    drawMenu();
    break;
  case objective:
    drawObjective();
    break;
  case subjective:
    // drawSubjective();
    break;
  }

  display.display();
}

// ==========================================
//               MAIN SETUP
// ==========================================

void setup() {
  Serial.begin(115200);

  // Input Setup
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), updateEncoder, CHANGE);

  // Display Setup
  display.begin(SH1106_SWITCHCAPVCC);
  display.clearDisplay();
  display.setRotation(2);
  display.setTextColor(WHITE);
  display.display();

  Serial.println("--- SYSTEM START ---");

  // Network Setup
  connectToWiFi();
}

// ==========================================
//               MAIN LOOP
// ==========================================

void loop() {
  manageMQTT();   // Handles network reconnection in background
  handleInput();  // Checks buttons
  handleEncoder();
  updateDisplay(); // Draws screen at fixed FPS
}