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

const char* SSID          = "yardnet";
const char* PASS          = "YogiYogi835";
const char* MQTT_HOST     = "10.0.0.21";
const uint16_t MQTT_PORT  = 1883;
const char* DEVICE_ID     = "esp32-01";
const char* TOPIC_EVENTS  = "tracker/esp32-01/events";

#define PIN_ENC_A    32
#define PIN_ENC_B    33
#define PIN_ENC_SW   25

#define PIN_OLED_MOSI 23
#define PIN_OLED_CLK  18
#define PIN_OLED_DC   2
#define PIN_OLED_CS   5
#define PIN_OLED_RST  4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DISPLAY_FPS 30
#define MQTT_RETRY_INTERVAL 5000

// ==========================================
//               GLOBALS
// ==========================================

Adafruit_SH1106 display(PIN_OLED_MOSI, PIN_OLED_CLK, PIN_OLED_DC, PIN_OLED_RST, PIN_OLED_CS);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

enum menuState {
  menu,
  objective,
  subjective,
  trackCaffeine,
  trackMelatonin
};

menuState currentState = menu;

struct DataPoint {
  uint32_t timestamp;
  int16_t value;
};

const int MAX_RECORDS = 50;
DataPoint dataLog[MAX_RECORDS];
int logIndex = 0;

// --- STATE VARIABLES ---
int menuSelection = 0;
int subMenuSelection = 0;
int caffeineValue = 0;
int melatoninValue = 0;

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

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void manageMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
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

// Generalized Save Function
// Added 'type' parameter so you know what you are saving in the logs
void saveData(int value, String type) {
  if (logIndex >= MAX_RECORDS) return;

  dataLog[logIndex].timestamp = millis();
  dataLog[logIndex].value = value;

  // Flash Screen Feedback
  display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.display();
  delay(50);

  Serial.println("SAVING " + type + ": " + String(value));

  if (mqtt.connected()) {
    char payload[100];
    // FIXED BUG: Now sends 'value' (clean number) instead of 'encoderCounter' (raw clicks)
    // Also added "type" to JSON so you know if it's caffeine or melatonin
    snprintf(payload, sizeof(payload), "{\"type\":\"%s\", \"val\":%d, \"ts\":%lu}", type.c_str(), value, millis());
    mqtt.publish(TOPIC_EVENTS, payload);
  }

  logIndex++;
}

void handleInput() {
  if (digitalRead(PIN_ENC_SW) == LOW) {
    if (millis() - lastButtonPress > 300) {

      switch (currentState) {

        // --- MAIN MENU ---
        case menu:
          if (menuSelection == 0) {
            currentState = objective;
            subMenuSelection = 0;
          } else {
            currentState = subjective;
          }
          break;

        // --- OBJECTIVE MENU ---
        case objective:
          if (subMenuSelection == 0) {
            caffeineValue = 0; // Reset
            currentState = trackCaffeine;
          }
          else if (subMenuSelection == 1) {
            melatoninValue = 0; // Reset
            currentState = trackMelatonin;
          }
          else if (subMenuSelection == 2) {
            currentState = menu; // Back
          }
          break;

        // --- CAFFEINE SAVE ---
        case trackCaffeine:
          saveData(caffeineValue, "caffeine");
          currentState = menu;
          break;

        // --- MELATONIN SAVE (NEW) ---
        case trackMelatonin:
          saveData(melatoninValue, "melatonin");
          currentState = menu;
          break;
      }
      lastButtonPress = millis();
    }
  }
}

void handleEncoder() {
  int currentCounter = encoderCounter / 2; // Divide by 2 for smooth clicks
  int delta = currentCounter - lastCounter;

  if (delta != 0) {
    switch (currentState) {

      // --- MENUS ---
      case menu:
        if (delta > 0) menuSelection = 1;
        else if (delta < 0) menuSelection = 0;
        break;

      case objective:
        if (delta > 0) subMenuSelection++;
        else if (delta < 0) subMenuSelection--;
        if (subMenuSelection > 2) subMenuSelection = 2;
        if (subMenuSelection < 0) subMenuSelection = 0;
        break;

      // --- TRACKERS ---
      case trackCaffeine:
        if (delta > 0) caffeineValue += 10;
        else if (delta < 0) caffeineValue -= 10;
        if (caffeineValue < 0) caffeineValue = 0;
        if (caffeineValue > 400) caffeineValue = 400;
        break;

      case trackMelatonin:
        // Melatonin usually smaller doses (1mg steps)
        if (delta > 0) melatoninValue += 1;
        else if (delta < 0) melatoninValue -= 1;
        if (melatoninValue < 0) melatoninValue = 0;
        if (melatoninValue > 20) melatoninValue = 20; // Max 20mg
        break;
    }
    lastCounter = currentCounter;
  }
}

// ==========================================
//             DRAWING FUNCTIONS
// ==========================================

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 12, 128, 12, WHITE);

  if (menuSelection == 0) {
    display.fillRect(0, 16, 128, 20, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 22);
  display.println("OBJECTIVE");

  if (menuSelection == 1) {
    display.fillRect(0, 40, 128, 20, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 46);
  display.println("SUBJECTIVE");
}

void drawObjective() {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(35, 2);
  display.println("LOG INTAKE");
  display.drawLine(0, 12, 128, 12, WHITE);

  if (subMenuSelection == 0) {
    display.fillRect(0, 16, 128, 14, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 19);
  display.println("CAFFEINE (mg)");

  if (subMenuSelection == 1) {
    display.fillRect(0, 32, 128, 14, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 35);
  display.println("MELATONIN (mg)");

  if (subMenuSelection == 2) {
    display.fillRect(0, 48, 128, 14, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(10, 51);
  display.println("< BACK");
}

void drawCaffeineTracker() {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(30, 0);
  display.println("LOG CAFFEINE");
  display.drawLine(0, 10, 128, 10, WHITE);

  int numX = (caffeineValue < 100) ? 48 : 40;
  if (caffeineValue < 10) numX = 55;

  display.setTextSize(3);
  display.setCursor(numX, 20);
  display.print(caffeineValue);

  display.setTextSize(1);
  display.setCursor(numX + 55, 34);
  display.print("mg");

  display.drawRect(10, 50, 108, 8, WHITE);
  int barWidth = map(caffeineValue, 0, 400, 0, 104);
  if (barWidth > 104) barWidth = 104;
  display.fillRect(12, 52, barWidth, 4, WHITE);
}

void drawMelatoninTracker() {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(25, 0);
  display.println("LOG MELATONIN");
  display.drawLine(0, 10, 128, 10, WHITE);

  int numX = (melatoninValue < 10) ? 55 : 48;

  display.setTextSize(3);
  display.setCursor(numX, 20);
  display.print(melatoninValue);

  display.setTextSize(1);
  display.setCursor(numX + 40, 34);
  display.print("mg");

  // Melatonin Bar (Scale 0-20)
  display.drawRect(10, 50, 108, 8, WHITE);
  int barWidth = map(melatoninValue, 0, 20, 0, 104);
  if (barWidth > 104) barWidth = 104;
  display.fillRect(12, 52, barWidth, 4, WHITE);
}

void updateDisplay() {
  display.clearDisplay();

  if (mqtt.connected()) {
    display.fillCircle(SCREEN_WIDTH - 4, 4, 2, WHITE);
  }

  switch (currentState) {
    case menu:           drawMenu(); break;
    case objective:      drawObjective(); break;
    case subjective:     /* drawSubjective(); */ break;
    case trackCaffeine:  drawCaffeineTracker(); break;
    case trackMelatonin: drawMelatoninTracker(); break;
  }

  display.display();
}

// ==========================================
//               MAIN SETUP
// ==========================================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), updateEncoder, CHANGE);

  display.begin(SH1106_SWITCHCAPVCC);
  display.clearDisplay();
  display.setRotation(2);
  display.setTextColor(WHITE);
  display.display();

  Serial.println("--- SYSTEM START ---");
  connectToWiFi();
}

void loop() {
  manageMQTT();
  handleInput();
  handleEncoder();
  updateDisplay();
}