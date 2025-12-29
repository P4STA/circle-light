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
#define PIN        1           // keep as requested
#define NUMPIXELS  24
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// === IR receiver config ===
#define IR_RECEIVE_PIN 12
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

enum Mode {
  MODE_SINGLECOLOR,
  MODE_2LEDS,
  MODE_3LEDS,
  MODE_4LEDS,
  MODE_5LEDS,
  MODE_HUEMIRROR,
  MODE_HUEMIRROR7,
  MODE_HUEMIRRORFADED,
  MODE_ALTERNATE,
  MODE_RAINBOW,
  MODE_RANDOM
};
// Helper: get color at opposite hue
uint32_t getOppositeHueColor(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  float maxc = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
  float minc = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
  float h = 0, s = 0, v = maxc;
  float d = maxc - minc;
  if (d > 0.0001f) {
    s = maxc > 0 ? d / maxc : 0;
    if (maxc == rf) h = (gf - bf) / d + (gf < bf ? 6 : 0);
    else if (maxc == gf) h = (bf - rf) / d + 2;
    else h = (rf - gf) / d + 4;
    h /= 6;
  }
  float h2 = h + 0.5f; if (h2 > 1.0f) h2 -= 1.0f;
  float r2, g2, b2;
  int i = int(h2 * 6);
  float f = h2 * 6 - i;
  float p = v * (1 - s);
  float q = v * (1 - f * s);
  float t = v * (1 - (1 - f) * s);
  switch(i % 6) {
    case 0: r2 = v, g2 = t, b2 = p; break;
    case 1: r2 = q, g2 = v, b2 = p; break;
    case 2: r2 = p, g2 = v, b2 = t; break;
    case 3: r2 = p, g2 = q, b2 = v; break;
    case 4: r2 = t, g2 = p, b2 = v; break;
    case 5: r2 = v, g2 = p, b2 = q; break;
  }
  return ((uint8_t)(r2 * 255) << 16) | ((uint8_t)(g2 * 255) << 8) | (uint8_t)(b2 * 255);
}

static Mode currentMode = MODE_RAINBOW;
static uint32_t lastStaticColor = 0xFFFFFFFF;
static uint8_t lastStaticBrightness = 0xFF;
static bool fadeEnabled = false;
static uint32_t currentColor = 0;     // GRB packed
static bool isOn = true;
static uint8_t brightness = 128;      // 0..255

// Timers for animations
static uint32_t lastAnimMs = 0;
static uint16_t hue16 = 0;            // 0..65535
static uint8_t jumpIndex = 0;

// === Persistent state (EEPROM) ===
#define EEPROM_SIZE_BYTES  32
#define EEPROM_MAGIC       0xA5
#define EEPROM_VERSION     1

struct __attribute__((packed)) PersistedState {
  uint8_t magic;       // 0xA5 when valid
  uint8_t version;     // version for future changes
  uint8_t mode;        // Mode enum value
  uint8_t brightness;  // 0..255
  uint8_t isOn;        // 0/1
  uint8_t reserved;    // alignment
  uint32_t color;      // GRB
  uint16_t hue16;      // for fade resume (optional)
  uint8_t checksum;    // simple checksum over preceding bytes
};

static bool stateDirty = false;
static uint32_t lastStateChangeMs = 0;

static uint8_t computeChecksum(const PersistedState &s) {
  uint8_t sum = 0;
  sum ^= s.magic;
  sum ^= s.version;
  sum ^= s.mode;
  sum ^= s.brightness;
  sum ^= s.isOn;
  sum ^= s.reserved;
  // fold 32-bit color and 16-bit hue
  sum ^= (uint8_t)(s.color & 0xFF);
  sum ^= (uint8_t)((s.color >> 8) & 0xFF);
  sum ^= (uint8_t)((s.color >> 16) & 0xFF);
  sum ^= (uint8_t)((s.color >> 24) & 0xFF);
  sum ^= (uint8_t)(s.hue16 & 0xFF);
  sum ^= (uint8_t)((s.hue16 >> 8) & 0xFF);
  return sum;
}

static void persistStateScheduleSave() {
  stateDirty = true;
  lastStateChangeMs = millis();
}

static void persistStateTryCommit() {
  if (!stateDirty) return;
  if (millis() - lastStateChangeMs < 1000) return; // debounce writes

  PersistedState s{};
  s.magic = EEPROM_MAGIC;
  s.version = EEPROM_VERSION;
  s.mode = static_cast<uint8_t>(currentMode);
  s.brightness = brightness;
  s.isOn = isOn ? 1 : 0;
  s.reserved = 0;
  s.color = currentColor;
  s.hue16 = hue16;
  s.checksum = computeChecksum(s);

  EEPROM.put(0, s);
  EEPROM.commit();
  stateDirty = false;
}

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
  Adafruit_NeoPixel::Color(255, 0, 200)    // IR_B4 - purple2
};

static const uint8_t PALETTE_SIZE = sizeof(palette) / sizeof(palette[0]);

void showColor(uint32_t c) {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, c);
  pixels.show();
}

void applyPowerState() {
  pixels.setBrightness(isOn ? brightness : 0);
  if (isOn) showColor(currentColor); else pixels.show();
}

void setStaticColor(uint32_t color) {
  currentColor = color;
  applyPowerState();
}

void handleCode(uint32_t code) {
  bool changed = false;
  switch (code) {
    case IR_ON:    isOn = true; applyPowerState(); changed = true; break;
    case IR_OFF:   isOn = false; applyPowerState(); changed = true; break;

    case IR_R1: setStaticColor(palette[0]); changed = true; break;
    case IR_G1: setStaticColor(palette[1]); changed = true; break;
    case IR_B1: setStaticColor(palette[2]); changed = true; break;
    case IR_R2: setStaticColor(palette[3]); changed = true; break;
    case IR_G2: setStaticColor(palette[4]); changed = true; break;
    case IR_B2: setStaticColor(palette[5]); changed = true; break;
    case IR_R3: setStaticColor(palette[6]); changed = true; break;
    case IR_G3: setStaticColor(palette[7]); changed = true; break;
    case IR_B3: setStaticColor(palette[8]); changed = true; break;
    case IR_R4: setStaticColor(palette[9]); changed = true; break;
    case IR_G4: setStaticColor(palette[10]); changed = true; break;
    case IR_B4: setStaticColor(palette[11]); changed = true; break;

    case IR_WHITE: setStaticColor(Adafruit_NeoPixel::Color(255, 255, 255)); changed = true; break;

    case IR_FADE:
      fadeEnabled = !fadeEnabled;
      changed = true;
      break;
    case IR_JUMP: {
      // Cycle through all modes including STATIC
      static uint8_t modeCycle = 0;
      modeCycle = (modeCycle + 1) % 11; // 11 modes: 0-10
      switch (modeCycle) {
        case 0: currentMode = MODE_SINGLECOLOR; break;
        case 1: currentMode = MODE_RAINBOW; break;
        case 2: currentMode = MODE_2LEDS; break;
        case 3: currentMode = MODE_3LEDS; break;
        case 4: currentMode = MODE_4LEDS; break;
        case 5: currentMode = MODE_5LEDS; break;
        case 6: currentMode = MODE_HUEMIRROR; break;
        case 7: currentMode = MODE_HUEMIRROR7; break;
        case 8: currentMode = MODE_HUEMIRRORFADED; break;
        case 9: currentMode = MODE_ALTERNATE; break;
        case 10: currentMode = MODE_RANDOM; break;
      }
      changed = true;
      break;
    }
    case IR_FLASH:
      // currentMode = MODE_FLASH;
      jumpIndex = 0;
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
  if (changed) persistStateScheduleSave();
}

void setup() {
  Serial.begin(115200);
  pixels.begin();
  EEPROM.begin(EEPROM_SIZE_BYTES);

  // Load persisted state if valid
  PersistedState s{};
  EEPROM.get(0, s);
  bool valid = (s.magic == EEPROM_MAGIC) && (s.version == EEPROM_VERSION) && (computeChecksum(s) == s.checksum);
  if (valid) {
    currentMode = static_cast<Mode>(s.mode);
    brightness = s.brightness;
    isOn = s.isOn == 1;
    currentColor = s.color;
    hue16 = s.hue16;
  } else {
    // defaults
    brightness = 128;
    isOn = true;
    currentMode = MODE_SINGLECOLOR;
    currentColor = Adafruit_NeoPixel::Color(255, 255, 255);
  }

  pixels.setBrightness(brightness);
  if (isOn) showColor(currentColor); else pixels.show();

  irrecv.enableIRIn();
}

void loop() {
        
  // Handle IR input - check this FIRST and frequently
  if (irrecv.decode(&irResults)) {
    uint32_t value = irResults.value;
    // Handle repeat codes too - they should maintain the last command
    if (value != 0xFFFFFFFF) {
      handleCode(value);
    }
    irrecv.resume();
  }

  // Commit pending state even if lights are off
  persistStateTryCommit();

  // Handle animations - but don't let them block IR processing
  if (!isOn) return;

  uint32_t nowMs = millis();
  switch (currentMode) {
    case MODE_SINGLECOLOR: {
      // Show one color on all LEDs, static or fading
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
      }
      if (baseColor != lastStaticColor || brightness != lastStaticBrightness) {
        pixels.setBrightness(isOn ? brightness : 0);
        showColor(baseColor);
        lastStaticColor = baseColor;
        lastStaticBrightness = brightness;
      }
      break;
    }
    case MODE_2LEDS:
    case MODE_3LEDS:
    case MODE_4LEDS:
    case MODE_5LEDS: {
      // Evenly spaced LEDs for each mode
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
      }
      pixels.setBrightness(isOn ? brightness : 0);
      bool ledOn[NUMPIXELS] = {0};
      if (currentMode == MODE_2LEDS) {
        ledOn[6] = true;
        ledOn[18] = true;
      } else if (currentMode == MODE_3LEDS) {
        ledOn[0] = true;
        ledOn[8] = true;
        ledOn[16] = true;
      } else if (currentMode == MODE_4LEDS) {
        ledOn[0] = true;
        ledOn[6] = true;
        ledOn[12] = true;
        ledOn[18] = true;
      } else if (currentMode == MODE_5LEDS) {
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
      break;
    }
    case MODE_HUEMIRROR: {
      // LEDs 6 and 18 on, one is baseColor, the other is opposite hue
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
      }
      uint32_t oppColor = getOppositeHueColor(baseColor);
      pixels.setBrightness(isOn ? brightness : 0);
      for (int j = 0; j < NUMPIXELS; j++) {
        if (j == 6)
          pixels.setPixelColor(j, baseColor);
        else if (j == 18)
          pixels.setPixelColor(j, oppColor);
        else
          pixels.setPixelColor(j, 0);
      }
      pixels.show();
      delay(100);
      break;
    }
    case MODE_HUEMIRROR7: {
      // LEDs 0-6 use baseColor, 12-18 use opposite hue
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
      }
      uint32_t oppColor = getOppositeHueColor(baseColor);
      pixels.setBrightness(isOn ? brightness : 0);
      for (int j = 0; j < NUMPIXELS; j++) {
        if (j >= 0 && j <= 6)
          pixels.setPixelColor(j, baseColor);
        else if (j >= 12 && j <= 18)
          pixels.setPixelColor(j, oppColor);
        else
          pixels.setPixelColor(j, 0);
      }
      pixels.show();
      delay(100);
      break;
    }
    case MODE_HUEMIRRORFADED: {
      // All LEDs on: 0-11 base color, 12-23 mirrored hue
      // Brightness ramps up from 0 to 6, down to 11, up 12 to 18, down to 23
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
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
      break;
    }
    case MODE_ALTERNATE: {
      // Even LEDs: base color, Odd LEDs: opposite hue
      uint32_t baseColor = currentColor;
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          hue16 += 128;
          baseColor = pixels.gamma32(pixels.ColorHSV(hue16));
        } else {
          break;
        }
      }
      uint32_t oppColor = getOppositeHueColor(baseColor);
      pixels.setBrightness(isOn ? brightness : 0);
      for (int i = 0; i < NUMPIXELS; i++) {
        if (i % 2 == 0)
          pixels.setPixelColor(i, baseColor);
        else
          pixels.setPixelColor(i, oppColor);
      }
      pixels.show();
      delay(100);
      break;
    }
    case MODE_RAINBOW: {
      // Rainbow mode: static rainbow if fade is off, animated/moving rainbow if fade is on
      static uint16_t rainbowOffset = 0;
      bool update = false;
      pixels.setBrightness(isOn ? brightness : 0);
      if (fadeEnabled) {
        if (nowMs - lastAnimMs >= 50) {
          lastAnimMs = nowMs;
          rainbowOffset += 256; // Move the rainbow
          update = true;
        } else {
          break;
        }
      } else {
        // Only update if brightness changes (static rainbow)
        if (brightness != lastStaticBrightness) {
          lastStaticBrightness = brightness;
          update = true;
        }
      }
      if (update) {
        for (int i = 0; i < NUMPIXELS; i++) {
          // Spread the rainbow evenly around the ring
          uint16_t hue = ((uint32_t)i * 65536UL / NUMPIXELS);
          if (fadeEnabled) hue += rainbowOffset;
          uint32_t color = pixels.gamma32(pixels.ColorHSV(hue));
          pixels.setPixelColor(i, color);
        }
        pixels.show();
      }
      delay(100);
      break;
    }
    case MODE_RANDOM: {
      // Random LEDs light up with random color and then fade
      static uint8_t ledStates[NUMPIXELS] = {0}; // 0=off, 1=on, 2=fade
      static uint32_t ledColors[NUMPIXELS] = {0};
      static uint8_t ledBrightness[NUMPIXELS] = {0};
      static uint32_t lastRandomMs = 0;
      const uint16_t fadeStep = 20; // ms per fade step (slower)
      const uint8_t fadeAmount = 3; // fade decrement per step (slower)
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
      delay(60);
      break;
    }
  }
  
  // Small delay to prevent overwhelming the IR receiver
  delay(10);
}
