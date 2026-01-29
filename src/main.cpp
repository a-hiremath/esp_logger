#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>    // Note the capitalization fix (PlatformIO is case-sensitive)
#include <Adafruit_SH1106.h> // Ensure you have this specific library installed

// --- ESP32 PIN DEFINITIONS ---
// Encoder Pins (Safe generic GPIOs)
#define ENC_A       32
#define ENC_B       33
#define ENC_SW      25

// Display Pins (VSPI Hardware SPI)
#define OLED_MOSI   23  // Hardware MOSI
#define OLED_CLK    18  // Hardware SCK
#define OLED_DC     2
#define OLED_CS     5
#define OLED_RESET  4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Constructor: Using Hardware SPI is faster on ESP32
// (We pass &SPI to tell it to use the hardware bus)
Adafruit_SH1106 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// Volatile is crucial for variables shared with Interrupts
volatile int counter = 0;
volatile int lastEncoded = 0;

// --- INTERRUPT HANDLER ---
// ERROR PREVENTION: 'IRAM_ATTR' puts this function in RAM so it's fast enough for interrupts
void IRAM_ATTR updateEncoder() {
  int MSB = digitalRead(ENC_A); // MSB = most significant bit
  int LSB = digitalRead(ENC_B); // LSB = least significant bit

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) counter++;
  if((sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) && counter > 0) counter--;

  lastEncoded = encoded;
}

// Helper to center text
void print_centered(String text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

// Data Logging Structure
struct dataPoint {
  uint32_t timestamp;
  int16_t value;
};

const int MAX_RECORDS = 10;
dataPoint dataLog[MAX_RECORDS];
int logIndex = 0;

unsigned long lastButtonPress = 0;
const int debounceDelay = 300;

void handleButtonPress() {
  // 1. Check hardware state
  if (digitalRead(ENC_SW) == LOW) { // Encoder buttons are usually active LOW

    // 2. Check Debounce Timer
    if (millis() - lastButtonPress > debounceDelay) {

      // 3. Check Memory Limit
      if (logIndex < MAX_RECORDS) {
        // --- SAVE DATA ---
        dataLog[logIndex].timestamp = millis();
        dataLog[logIndex].value = counter;

        // --- VISUAL CONFIRMATION ---
        // Only draw the feedback box, don't clear the whole screen yet to avoid flicker
        display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
        display.display();

        // Short blocking delay is okay in loop(), but bad in ISR.
        // Since this is the "Loop", it's safe.
        delay(50);

        // --- DEBUG LOG ---
        Serial.printf("Saved! [%d] Time: %lu Val: %d\n", logIndex, dataLog[logIndex].timestamp, dataLog[logIndex].value);

        logIndex++;

      } else {
        Serial.println("Memory Full!");
      }
      lastButtonPress = millis();
    }
  }
}

void setup() {
  Serial.begin(115200); // Standard ESP32 speed

  // Configure Pins
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // Attach Interrupts
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);

  // Display Setup
  // SH1106 often needs a specific init call, sometimes just begin() works depending on the library version
  // If SWITCHCAPVCC throws an error, try removing the arguments or using SSD1306_SWITCHCAPVCC
  display.begin(SH1106_SWITCHCAPVCC);

  display.clearDisplay();
  display.setRotation(2); // Adjust if your screen is upside down
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.display();

  Serial.println("System Ready");
}

void loop() {
  display.clearDisplay();

  // Print Counter
  print_centered(String(counter), 32, 2); // Bumped size to 2 for visibility

  // Print Time
  display.setCursor(0,0);
  display.setTextSize(1);
  display.print("Time: ");
  display.print(millis() / 1000); // Print seconds

  handleButtonPress();

  display.display();
}