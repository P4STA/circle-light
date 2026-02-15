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
#define PIN        D2
#define NUMPIXELS  24
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// === IR receiver config ===
#define IR_RECEIVE_PIN D1
IRrecv irrecv(IR_RECEIVE_PIN);
decode_results irResults;

// === Motor config ===
#define MOTOR_PIN D7
#define MOTOR_SPEED_LEVELS 5
static uint8_t motorSpeedLevel = 0;  // 0-4 (off to max)
// PWM values for each speed level (0, 64, 128, 192, 255)
const uint8_t motorSpeedPWM[MOTOR_SPEED_LEVELS] = {0, 64, 128, 192, 255};

// === Remote codes (NEC) ===
// Row 1
#define IR_ON      0xFFA25D
#define IR_MIDDLE1 0xFF629D  // Mode cycle button
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

// === State variables ===
static uint8_t currentMode = 0;
static uint8_t numberModifier = 2;
static bool fadeEnabled = false;
static bool oppositeHueEnabled = false;
static bool randomColorsEnabled = true;
static uint8_t brightness = 128;

// Hue-based color storage
static uint16_t currentHue = 0;       // Current active hue (0-65535)
static uint16_t selectedHue = 0;      // Hue selected by color buttons
static uint8_t currentSat = 255;      // Saturation (255 = full color, 0 = white)
static uint8_t selectedSat = 255;     // Saturation selected by color buttons

// Timers
static uint32_t lastHueCycleMs = 0;

// Hue values for remote buttons (0-65535, where 65536 = 360°)
// Approximated from the typical 24-key RGB remote layout
#define HUE_RED       0
#define HUE_GREEN     21845    // 120°
#define HUE_BLUE      43690    // 240°
#define HUE_ORANGE    2730     // ~15°
#define HUE_YELLOW    5461     // ~30°
#define HUE_LIME      10922    // ~60°
#define HUE_CYAN      32768    // 180°
#define HUE_TEAL      27306    // ~150°
#define HUE_SKYBLUE   38229    // ~210°
#define HUE_PINK      58982    // ~324°
#define HUE_PURPLE    54613    // ~300°
#define HUE_MAGENTA   49152    // ~270°

// Helper: convert current hue/sat to RGB color
uint32_t hueToColor(uint16_t hue, uint8_t sat) {
  return pixels.gamma32(pixels.ColorHSV(hue, sat, 255));
}

// Helper: get opposite hue (180° shift)
uint16_t oppositeHue(uint16_t hue) {
  return hue + 32768; // Wraps naturally due to uint16_t overflow
}

void applyBrightness() {
  pixels.setBrightness(brightness);
  pixels.show();
}

void updateMotorSpeed() {
  analogWrite(MOTOR_PIN, motorSpeedPWM[motorSpeedLevel]);
  Serial.print("Motor speed level: ");
  Serial.print(motorSpeedLevel);
  Serial.print(" (PWM: ");
  Serial.print(motorSpeedPWM[motorSpeedLevel]);
  Serial.println(")");
}

void setHue(uint16_t hue, uint8_t sat) {
  selectedHue = hue;
  selectedSat = sat;
  currentHue = hue;
  currentSat = sat;
}

void handleIRCode(uint32_t code) {
  Serial.println();
  Serial.print("IR code received: 0x");
  Serial.println(code, HEX);
  
  switch (code) {
    case IR_ON:
      if (motorSpeedLevel < MOTOR_SPEED_LEVELS - 1) motorSpeedLevel++;
      updateMotorSpeed();
      break;
    case IR_OFF:
      if (motorSpeedLevel > 0) motorSpeedLevel--;
      updateMotorSpeed();
      break;

    // Color buttons
    case IR_R1: setHue(HUE_RED, 255); break;
    case IR_G1: setHue(HUE_GREEN, 255); break;
    case IR_B1: setHue(HUE_BLUE, 255); break;
    case IR_R2: setHue(HUE_ORANGE, 255); break;
    case IR_G2: setHue(HUE_YELLOW, 255); break;
    case IR_B2: setHue(HUE_LIME, 255); break;
    case IR_R3: setHue(HUE_CYAN, 255); break;
    case IR_G3: setHue(HUE_TEAL, 255); break;
    case IR_B3: setHue(HUE_SKYBLUE, 255); break;
    case IR_R4: setHue(HUE_PINK, 255); break;
    case IR_G4: setHue(HUE_PURPLE, 255); break;
    case IR_B4: setHue(HUE_MAGENTA, 255); break;
    case IR_WHITE: setHue(0, 0); break; // Saturation 0 = white

    case IR_FADE:
      fadeEnabled = !fadeEnabled;
      Serial.print("Fade ");
      Serial.println(fadeEnabled ? "enabled" : "disabled");
      if (!fadeEnabled) {
        currentHue = selectedHue;
        currentSat = selectedSat;
      }
      break;

    case IR_MIDDLE1:
      currentMode = (currentMode + 1) % 6;
      Serial.print("Mode: ");
      Serial.println(currentMode);
      break;
    
    case IR_JUMP:
      numberModifier = (numberModifier % 12) + 1;
      Serial.print("Number modifier: ");
      Serial.println(numberModifier);
      break;

    case IR_FLASH:
      if (currentMode == 5) {
        randomColorsEnabled = !randomColorsEnabled;
        Serial.print("Random colors ");
        Serial.println(randomColorsEnabled ? "enabled" : "disabled");
      } else {
        oppositeHueEnabled = !oppositeHueEnabled;
        Serial.print("Opposite hue ");
        Serial.println(oppositeHueEnabled ? "enabled" : "disabled");
      }
      break;

    case IR_DIM:
      if (brightness >= 16) brightness -= 16; else brightness = 0;
      applyBrightness();
      break;
    case IR_BRIGHT:
      if (brightness <= 239) brightness += 16; else brightness = 255;
      applyBrightness();
      break;
  }
}

void animationStaticColor() {
  pixels.setBrightness(brightness);
  uint32_t color1 = hueToColor(currentHue, currentSat);
  uint32_t color2 = oppositeHueEnabled ? hueToColor(oppositeHue(currentHue), currentSat) : color1;
  
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, (i < 12) ? color1 : color2);
  }
  pixels.show();
}

void animationNLEDSGROUP() {
  pixels.setBrightness(brightness);
  uint32_t color1 = hueToColor(currentHue, currentSat);
  uint32_t color2 = oppositeHueEnabled ? hueToColor(oppositeHue(currentHue), currentSat) : color1;
  
  for (int i = 0; i < NUMPIXELS; i++) {
    bool inGroup1 = (i < numberModifier);
    bool inGroup2 = (i >= 12 && i < 12 + numberModifier);
    if (inGroup1) {
      pixels.setPixelColor(i, color1);
    } else if (inGroup2) {
      pixels.setPixelColor(i, color2);
    } else {
      pixels.setPixelColor(i, 0);
    }
  }
  pixels.show();
}

void animationNLEDSGAPS() {
  pixels.setBrightness(brightness);
  uint32_t color1 = hueToColor(currentHue, currentSat);
  uint32_t color2 = oppositeHueEnabled ? hueToColor(oppositeHue(currentHue), currentSat) : color1;
  
  float gap = NUMPIXELS / (float)numberModifier;
  for (int i = 0; i < NUMPIXELS; i++) {
    bool isLit = false;
    uint8_t litIndex = 0;
    for (uint8_t k = 0; k < numberModifier; k++) {
      if (i == (int)(k * gap)) {
        isLit = true;
        litIndex = k;
        break;
      }
    }
    if (isLit) {
      pixels.setPixelColor(i, (litIndex % 2 == 0) ? color1 : color2);
    } else {
      pixels.setPixelColor(i, 0);
    }
  }
  pixels.show();
}

void animationFadedMirror() {
  pixels.setBrightness(brightness);
  uint16_t hue2 = oppositeHueEnabled ? oppositeHue(currentHue) : currentHue;
  
  for (int j = 0; j < NUMPIXELS; j++) {
    float bright = 0;
    if (j <= 11) {
      if (j <= 6) bright = (j / 6.0f);
      else bright = ((11 - j) / 5.0f);
    } else {
      if (j <= 18) bright = ((j - 12) / 6.0f);
      else bright = ((23 - j) / 5.0f);
    }
    uint8_t val = (uint8_t)(bright * 255);
    uint16_t hue = (j < 12) ? currentHue : hue2;
    pixels.setPixelColor(j, pixels.gamma32(pixels.ColorHSV(hue, currentSat, val)));
  }
  pixels.show();
}

void animationRainbow() {
  static uint16_t rainbowOffset = 0;
  
  if (fadeEnabled) {
    rainbowOffset += 256;
  }
  
  pixels.setBrightness(brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    uint16_t hue = ((uint32_t)i * 65536UL / NUMPIXELS) + rainbowOffset;
    pixels.setPixelColor(i, pixels.gamma32(pixels.ColorHSV(hue)));
  }
  pixels.show();
}

void animationRandom() {
  static uint8_t ledStates[NUMPIXELS] = {0};
  static uint16_t ledHues[NUMPIXELS] = {0};
  static uint8_t ledBrightness[NUMPIXELS] = {0};
  static uint32_t lastRandomMs = 0;
  const uint8_t fadeAmount = 3;

  uint32_t nowMs = millis();

  if (nowMs - lastRandomMs > 200) {
    lastRandomMs = nowMs;
    uint8_t n = 1 + (rand() % 2);
    for (uint8_t k = 0; k < n; ++k) {
      uint8_t idx = rand() % NUMPIXELS;
      ledStates[idx] = 1;
      ledHues[idx] = randomColorsEnabled ? (rand() % 65536) : currentHue;
      ledBrightness[idx] = 255;
    }
  }
  
  for (int i = 0; i < NUMPIXELS; i++) {
    if (ledStates[i] == 1) {
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
    uint8_t sat = randomColorsEnabled ? 255 : currentSat;
    pixels.setPixelColor(i, pixels.gamma32(pixels.ColorHSV(ledHues[i], sat, ledBrightness[i])));
  }
  pixels.setBrightness(brightness);
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  pixels.begin();
  irrecv.enableIRIn();
  
  // Motor setup
  pinMode(MOTOR_PIN, OUTPUT);
  analogWriteFrequency(20000);  // 20kHz PWM
  analogWrite(MOTOR_PIN, 0);    // Start with motor off
}

void loop() {
  if (irrecv.decode(&irResults)) {
    uint32_t value = irResults.value;
    if (value != 0xFFFFFFFF) {
      handleIRCode(value);
    }
    irrecv.resume();
  }

  static uint32_t lastAnimationMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastAnimationMs < 20) return;
  lastAnimationMs = nowMs;

  // Centralized hue cycling (except Rainbow which handles its own)
  if (fadeEnabled && currentMode != 4) {
    if (nowMs - lastHueCycleMs >= 50) {
      lastHueCycleMs = nowMs;
      currentHue += 256;
      currentSat = 255; // Full saturation when cycling
    }
  }

  switch (currentMode) {
    case 0: animationStaticColor(); break;
    case 1: animationNLEDSGROUP(); break;
    case 2: animationNLEDSGAPS(); break;
    case 3: animationFadedMirror(); break;
    case 4: animationRainbow(); break;
    case 5: animationRandom(); break;
  }
}
