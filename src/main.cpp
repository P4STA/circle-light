#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

#ifdef __AVR__
  #include <avr/power.h>
#endif

// === NeoPixel config ===
#define PIN        1           // keep as requested
#define NUMPIXELS  24
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// === EEPROM config ===
// Store one byte with step index 0..11
#define EEPROM_SIZE   1
#define COLOR_ADDR    0

// 65536 / 12 ≈ 5461; use integer step
static const uint16_t HUE_STEP = 5461;

void fillColor(uint32_t c) {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, c);
  pixels.show();
}

void setup() {
  pixels.begin();
  // Optional brightness (0..255). Comment out if you want max.
  // pixels.setBrightness(80);

  EEPROM.begin(EEPROM_SIZE);

  // Read current step (0..11). If uninitialized (0xFF or >11), start at 0.
  uint8_t step = EEPROM.read(COLOR_ADDR);
  if (step > 11) step = 0;

  // Compute hue for this boot
  uint16_t hue = (uint16_t)(step * HUE_STEP);          // 0..65535 range
  uint32_t color = pixels.gamma32(pixels.ColorHSV(hue, 255, 255));

  // Show solid color
  fillColor(color);

  // Store next step for the *next* reset
  uint8_t next = (step + 1) % 12;
  EEPROM.write(COLOR_ADDR, next);
  EEPROM.commit();
}

void loop() {
  // Nothing to do; color is set once on boot.
}
