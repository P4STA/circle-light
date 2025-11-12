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
  MODE_STATIC,
  MODE_FADE,
  MODE_JUMP,
  MODE_FLASH
};

static Mode currentMode = MODE_STATIC;
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
  currentMode = MODE_STATIC;
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
      currentMode = MODE_FADE;
      // keep hue16; start from current color hue if desired in future
      changed = true;
      break;
    case IR_JUMP:
      currentMode = MODE_JUMP;
      jumpIndex = 0;
      changed = true;
      break;
    case IR_FLASH:
      currentMode = MODE_FLASH;
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
    currentMode = MODE_STATIC;
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
    case MODE_STATIC:
      // Nothing to do
      break;
    case MODE_FADE:
      if (nowMs - lastAnimMs >= 50) { // slightly slower for better responsiveness
        lastAnimMs = nowMs;
        hue16 += 128; // slower sweep
        uint32_t c = pixels.gamma32(pixels.ColorHSV(hue16));
        currentColor = c;
        showColor(c);
        // Don't spam EEPROM while animating; only store hue on demand
      }
      break;
    case MODE_JUMP:
      if (nowMs - lastAnimMs >= 500) {
        lastAnimMs = nowMs;
        currentColor = palette[jumpIndex % PALETTE_SIZE];
        jumpIndex++;
        showColor(currentColor);
      }
      break;
    case MODE_FLASH:
      if (nowMs - lastAnimMs >= 200) { // slightly slower flash
        lastAnimMs = nowMs;
        // Alternate current color with black to create a flash effect
        static bool onPhase = true;
        onPhase = !onPhase;
        if (onPhase) {
          currentColor = palette[jumpIndex % PALETTE_SIZE];
          jumpIndex++;
          showColor(currentColor);
        } else {
          showColor(Adafruit_NeoPixel::Color(0, 0, 0));
        }
      }
      break;
  }
  
  // Small delay to prevent overwhelming the IR receiver
  delay(10);
}
