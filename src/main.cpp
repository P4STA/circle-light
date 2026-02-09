#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
// IR
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#ifdef __AVR__
  #include <avr/power.h>
#endif

// === NeoPixel config ===
#define PIN        2
#define NUMPIXELS  24
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// === IR receiver config ===
#define IR_RECEIVE_PIN 3
IRrecv irrecv(IR_RECEIVE_PIN);
decode_results irResults;

// === Remote codes (NEC) ===
// Row 1
#define IR_ON      0xFFA25D
#define IR_MIDDLE1 0xFF629D  // Unused circle button at top middle
#define IR_OFF     0xFFE21D
// Row 2
#define IR_R1      0xFF22DD
#define IR_G1      0xFF02FD
#define IR_B1      0xFFC23D
// Row 3
#define IR_R2      0xFFE01F
#define IR_G2      0xFFA857
#define IR_B2      0xFF906F
// Row 4
#define IR_R3      0xFF6897
#define IR_G3      0xFF9867
#define IR_B3      0xFFB04F
// Row 5
#define IR_R4      0xFF30CF
#define IR_G4      0xFF18E7
#define IR_B4      0xFF7A85
// Row 6
#define IR_WHITE   0xFF10EF
#define IR_FADE    0xFF38C7
#define IR_JUMP    0xFF5AA5
// Row 7
#define IR_FLASH   0xFF42BD
#define IR_DIM     0xFF4AB5
#define IR_BRIGHT  0xFF52AD

// Static variables
static uint8_t currentMode = 0;
static uint8_t numberModifier = 2;  // Used for MODE_NLEDSGROUP and MODE_NLEDSGAPS
static uint32_t lastStaticColor = 0xFFFFFFFF;
static uint8_t lastStaticBrightness = 0xFF;
static bool fadeEnabled = false;
static uint32_t currentColor = Adafruit_NeoPixel::Color(255, 0, 0);     // GRB packed
static bool isOn = true;
static uint8_t brightness = 128;      // 0..255

// Timers for animations
static uint32_t lastAnimMs = 0;
static uint16_t hue16 = 0;            // 0..65535
static uint8_t jumpIndex = 0;

// A palette approximating the 24-key remote colors (left->right, top->bottom color rows)
static const uint32_t palette[] = {
  // Row2
  Adafruit_NeoPixel::Color(255, 0, 0),     // IR_R1
  Adafruit_NeoPixel::Color(0, 255, 0),     // IR_G1
  Adafruit_NeoPixel::Color(0, 0, 255),     // IR_B1
  // Row3 (darker variants)
  Adafruit_NeoPixel::Color(255, 50, 0),    // IR_R2 - orange
  Adafruit_NeoPixel::Color(255, 100, 0),   // IR_G2 - yellow
  Adafruit_NeoPixel::Color(255, 255, 0),   // IR_B2 - yellow-green
  // Row4
  Adafruit_NeoPixel::Color(0 , 255, 50),   // IR_R3 - teal1
  Adafruit_NeoPixel::Color(0, 255, 128),   // IR_G3 - teal2
  Adafruit_NeoPixel::Color(0, 64, 255),    // IR_B3 - sky blue
  // Row5
  Adafruit_NeoPixel::Color(255, 0, 128),   // IR_R4 - pink
  Adafruit_NeoPixel::Color(255, 0, 255),   // IR_G4 - purple1
  Adafruit_NeoPixel::Color(255, 0, 200),   // IR_B4 - purple2

  Adafruit_NeoPixel::Color(255, 255, 255)  // IR_WHITE
};

static const uint8_t PALETTE_SIZE = sizeof(palette) / sizeof(palette[0]);

void applyPowerState() {
  if (isOn) {
    pixels.setBrightness(brightness);
    pixels.show();
  } else {
    pixels.setBrightness(0);
    pixels.show();
  }
}

void handleIRCode(uint32_t code) {
  Serial.println();
  Serial.print("IR code received: 0x");
  Serial.println(code, HEX);
  bool changed = false;
  switch (code) {
    case IR_ON:    isOn = true; applyPowerState(); changed = true; break;
    case IR_OFF:   isOn = false; applyPowerState(); changed = true; break;

    case IR_R1: currentColor = palette[0]; changed = true; break;
    case IR_G1: currentColor = palette[1]; changed = true; break;
    case IR_B1: currentColor = palette[2]; changed = true; break;
    case IR_R2: currentColor = palette[3]; changed = true; break;
    case IR_G2: currentColor = palette[4]; changed = true; break;
    case IR_B2: currentColor = palette[5]; changed = true; break;
    case IR_R3: currentColor = palette[6]; changed = true; break;
    case IR_G3: currentColor = palette[7]; changed = true; break;
    case IR_B3: currentColor = palette[8]; changed = true; break;
    case IR_R4: currentColor = palette[9]; changed = true; break;
    case IR_G4: currentColor = palette[10]; changed = true; break;
    case IR_B4: currentColor = palette[11]; changed = true; break;
    case IR_WHITE: currentColor = palette[12]; changed = true; break;

    case IR_FADE:
      fadeEnabled = !fadeEnabled;
      Serial.print("Fade ");
      Serial.println(fadeEnabled ? "enabled" : "disabled");
      changed = true;
      break;

    case IR_JUMP:
      // Cycle through all modes
      currentMode = (currentMode + 1) % 6; // 6 modes: MODE_FULLCIRCLE, MODE_NLEDSGROUP, MODE_NLEDSGAPS, MODE_HUEMIRRORFADED, MODE_RAINBOW, MODE_RANDOM
      Serial.print("Mode: ");
      Serial.println(currentMode);
      changed = true;
      break;
    
    case IR_FLASH:
      // currentMode = MODE_FLASH;
      numberModifier = (numberModifier % 10) + 1; // Cycle 1-10
      Serial.print("Number modifier: ");
      Serial.println(numberModifier);
      changed = true;
      break;

    case IR_DIM:
      if (brightness >= 16) brightness -= 16; else brightness = 0;
      applyPowerState();
      changed = true;
      break;
    case IR_BRIGHT:
      if (brightness <= 239) brightness += 16; else brightness = 255;
      applyPowerState();
      changed = true;
      break;

    default:
      break;
  }
  
  //if (changed) persistStateScheduleSave();
}

void animationStaticColor() {
  // Show one color on all LEDs, static or fading
    pixels.setBrightness(isOn ? brightness : 0);
    for(int i=0; i<pixels.numPixels(); i++) { // For each pixel in strip...
      pixels.setPixelColor(i, currentColor);         //  Set pixel's color (in RAM)
      pixels.show();                          //  Update strip to match
    }
}

void animationNLEDSGROUP() {
  // Evenly spaced LEDs for each mode
  uint32_t baseColor = currentColor;
  if (fadeEnabled) {
    if (millis() - lastAnimMs >= 50) {
      lastAnimMs = millis();
      hue16 += 128;
      baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
    } else {
      return;
    }
  }
  pixels.setBrightness(isOn ? brightness : 0);
  bool ledOn[NUMPIXELS] = {0};

  if (numberModifier == 2) {
    ledOn[6] = true;
    ledOn[18] = true;
  } else if (numberModifier == 3) {
    ledOn[0] = true;
    ledOn[8] = true;
    ledOn[16] = true;
  } else if (numberModifier == 4) {
    ledOn[0] = true;
    ledOn[6] = true;
    ledOn[12] = true;
    ledOn[18] = true;
  } else if (numberModifier == 5) {
    ledOn[0] = true;
    ledOn[5] = true;
    ledOn[10] = true;
    ledOn[15] = true;
    ledOn[20] = true;
  }
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, ledOn[i] ? baseColor : 0);
  }
  pixels.show();
  delay(100);
}

void animationNLEDSGAPS() {
  // Evenly spaced gaps for each mode
  uint32_t baseColor = currentColor;
  if (fadeEnabled) {
    if (millis() - lastAnimMs >= 50) {
      lastAnimMs = millis();
      hue16 += 128;
      baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
    } else {
      return;
    }
  }
  pixels.setBrightness(isOn ? brightness : 0);
  bool ledOn[NUMPIXELS] = {0};

  float gap = NUMPIXELS / (numberModifier * 1.0f);
  Serial.print("Gap: ");   
  Serial.println(gap);
  for (uint8_t k = 0; k < numberModifier; k++) {
    ledOn[int(k * gap)] = true;
  }
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, ledOn[i] ? baseColor : 0);
  }
  pixels.show();
  delay(100);
}

/*
void animationFadedMirror() {
  // All LEDs on: 0-11 base color, 12-23 mirrored hue
  // Brightness ramps up from 0 to 6, down to 11, up 12 to 18, down to 23
  uint32_t nowMs = millis();
  uint32_t baseColor = currentColor;
  if (fadeEnabled) {
    if (nowMs - lastAnimMs >= 50) {
      lastAnimMs = nowMs;
      hue16 += 128;
      baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
    } else return; 
  }
  uint32_t oppColor = getOppositeHueColor(baseColor);
  for (int j = 0; j < NUMPIXELS; j++) {
    float bright = 0;
    if (j <= 11) {
      if (j <= 6) bright = (j / 6.0f); // 0 to 1
      else bright = ((11 - j) / 5.0f); // 1 to 0
    } else {
      if (j <= 18) bright = ((j - 12) / 6.0f); // 0 to 1
      else bright = ((23 - j) / 5.0f); // 1 to 0
    }
    uint8_t ledBright = (uint8_t)(bright * 255);
    uint32_t color = (j <= 11) ? baseColor : oppColor;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    pixels.setPixelColor(j, pixels.Color((r * ledBright) / 255, (g * ledBright) / 255, (b * ledBright) / 255));
  }
  pixels.setBrightness(isOn ? brightness : 0);
  pixels.show();
  delay(100);
}
  */

void animationRainbow() {
  // Rainbow mode: static rainbow if fade is off, animated/moving rainbow if fade is on
      static uint16_t rainbowOffset = 0;
      pixels.setBrightness(isOn ? brightness : 0);
      for (int i = 0; i < NUMPIXELS; i++) {
        // Spread the rainbow evenly around the ring
        uint16_t hue = ((uint32_t)i * 65536UL / NUMPIXELS);
        if (fadeEnabled) hue += rainbowOffset;
        uint32_t color = pixels.gamma32(pixels.ColorHSV(hue));
        pixels.setPixelColor(i, color);
      }
      pixels.show();
}

void animationRandom() {
  // Random LEDs light up with random color and then fade
  static uint8_t ledStates[NUMPIXELS] = {0}; // 0=off, 1=on, 2=fade
  static uint32_t ledColors[NUMPIXELS] = {0};
  static uint8_t ledBrightness[NUMPIXELS] = {0};
  static uint32_t lastRandomMs = 0;
  const uint16_t fadeStep = 20; // ms per fade step (slower)
  const uint8_t fadeAmount = 3; // fade decrement per step (slower)

  uint32_t nowMs = millis();

  if (nowMs - lastRandomMs > 200) { // slower random activation
    lastRandomMs = nowMs;
    // Randomly light up 1-2 LEDs
    uint8_t n = 1 + (rand() % 2);
    for (uint8_t k = 0; k < n; ++k) {
      uint8_t idx = rand() % NUMPIXELS;
      ledStates[idx] = 1;
      ledColors[idx] = pixels.gamma32(pixels.ColorHSV(rand() % 65536));
      ledBrightness[idx] = 255;
    }
  }
  // Update LEDs
  for (int i = 0; i < NUMPIXELS; i++) {
    if (ledStates[i] == 1) {
      // Just turned on, start fading
      ledStates[i] = 2;
    }
    if (ledStates[i] == 2) {
      if (ledBrightness[i] > fadeAmount) {
        ledBrightness[i] -= fadeAmount;
      } else {
        ledBrightness[i] = 0;
        ledStates[i] = 0;
      }
    }
    uint32_t c = ledColors[i];
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    uint8_t outR = (r * ledBrightness[i]) / 255;
    uint8_t outG = (g * ledBrightness[i]) / 255;
    uint8_t outB = (b * ledBrightness[i]) / 255;
    pixels.setPixelColor(i, pixels.Color(outR, outG, outB));
  }
  pixels.setBrightness(isOn ? brightness : 0);
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  pixels.begin();
  //EEPROM.begin(EEPROM_SIZE_BYTES);
  /*
  // Load persisted state if valid
  PersistedState s{};
  EEPROM.get(0, s);
  bool valid = (s.magic == EEPROM_MAGIC) && (s.version == EEPROM_VERSION) && (computeChecksum(s) == s.checksum);
  if (valid) {
    currentMode = s.mode;
    brightness = s.brightness;
    isOn = s.isOn == 1;
    currentColor = s.color;
    hue16 = s.hue16;
  } else {
    // defaults
    brightness = 128;
    isOn = true;
    currentMode = 0;
    currentColor = Adafruit_NeoPixel::Color(255, 255, 255);
  }

  pixels.setBrightness(brightness);
  if (isOn) showColor(currentColor); else pixels.show();
  */
  irrecv.enableIRIn();
}

void loop() {
  Serial.print(".");
  // Handle IR input - check this FIRST and frequently
  if (irrecv.decode(&irResults)) {
    uint32_t value = irResults.value;
    // Handle repeat codes too - they should maintain the last command
    if (value != 0xFFFFFFFF) {
      handleIRCode(value);
    }
    irrecv.resume();
  }

  // Commit pending state even if lights are off
  //persistStateTryCommit();

  // Handle animations - but don't let them block IR processing
  if (!isOn) return;

  static uint32_t lastAnimationMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastAnimationMs < 20) return;
  lastAnimationMs = nowMs;
  switch (currentMode) {
    case 0:
      animationStaticColor();
      break;
    case 1:
      animationNLEDSGROUP();
      break;
    case 2:
      animationNLEDSGAPS();
      break;
    case 3:
      //animationFadedMirror();
      break;
    case 4:
      animationRainbow();
      break;
    case 5:
      animationRandom();
      break;
  }
  delay(20); 
}
