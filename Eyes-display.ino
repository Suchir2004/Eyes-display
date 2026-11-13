/*
  RealisticEye & CuteEye - Multi-Mode Simulation (ESP32-C3)
  ==============================================================================
  - Minion Eye (No Yellow - Pure White Sclera with Goggle Rim)
  - Cute Anime Eye (Interactive Steerable Pupil with Glints)
  - Sleep Mode (Closed Sleeping Eye + Animated Floating Cartoon "Z z z")

  Wiring Connections:
  ==============================================================================
  Dual GC9A01 Round Displays (Wired in parallel to same ESP32-C3 pins):
    VCC      -> 3.3V
    GND      -> GND
    SCL/SCK  -> GPIO 4
    SDA/MOSI -> GPIO 6
    DC       -> GPIO 7
    RST      -> GPIO 2
    CS       -> GPIO 10
    BLK      -> 3.3V

  HW-040 Rotary Encoder:
    CLK / A  -> GPIO 0
    DT  / B  -> GPIO 1
    SW       -> GPIO 5   (Changed from GPIO 3 for 100% reliable detection)
    + / VCC  -> 3.3V
    GND      -> GND

  On-Board Button:
    GPIO 9 (Built-in BOOT button) also works as a secondary Sleep/Wake button!

  Interaction:
  ==============================================================================
  - ROTATE RIGHT (Clockwise)        -> Switches to CUTE EYE and steers gaze right
  - ROTATE LEFT  (Counter-Clockwise)-> Switches to MINION EYE and steers gaze left
  - PRESS BUTTON (SW or BOOT)       -> SLEEP MODE (Closed eye + animated "Z z z")
  - ROTATE ANY DIRECTION WHEN ASLEEP-> WAKES UP immediately in that direction!
  - LONG-PRESS BUTTON (> 1 sec)     -> Toggles Cute Eye Glint Style (1 or 2)
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "cartoon_eye.h"

// -----------------------------------------------------------------------------
// Display Pin Definitions (ESP32-C3)
// -----------------------------------------------------------------------------
#define TFT_SCK   4
#define TFT_MOSI  6
#define TFT_DC    7
#define TFT_RST   2
#define TFT_CS    10

// -----------------------------------------------------------------------------
// Rotary Encoder Pin Definitions (ESP32-C3)
// -----------------------------------------------------------------------------
#define PIN_ENCODER_CLK  0
#define PIN_ENCODER_DT   1
#define PIN_ENCODER_SW   5   // Changed from GPIO 3 to GPIO 5 for reliable button reading
#define PIN_BOARD_BOOT   9   // On-board BOOT button (built into every ESP32-C3 board)

// Set to true if knob rotation direction is reversed on your encoder
#define ENCODER_REVERSE  false

#define SPI_HZ     40000000UL   // 40 MHz SPI (80000000UL for maximum speed if short wires)
#define FRAME_MS   16           // 60 fps
#define BAND_H     30           // 240 / 30 = 8 bands for full screen (14.4 KB RAM)
#define DEBUG_FPS  1

// -----------------------------------------------------------------------------
// Hardware Driver & Buffers
// -----------------------------------------------------------------------------
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);

static uint16_t band[EYE_W * BAND_H];

// -----------------------------------------------------------------------------
// Fast Interrupt-Driven Rotary Encoder
// -----------------------------------------------------------------------------
volatile int encoderDelta = 0;
static volatile unsigned long lastEncoderMicros = 0;

void IRAM_ATTR isrEncoderCLK() {
  unsigned long now = micros();
  // 600 us debounce catches rapid spins with zero dropped steps
  if (now - lastEncoderMicros > 600) {
    if (digitalRead(PIN_ENCODER_DT) == HIGH) {
      encoderDelta += ENCODER_REVERSE ? -1 : 1;
    } else {
      encoderDelta += ENCODER_REVERSE ? 1 : -1;
    }
    lastEncoderMicros = now;
  }
}

// -----------------------------------------------------------------------------
// Eye Mode & Animation State
// -----------------------------------------------------------------------------
EyeMode currentMode = MODE_MINION;
CartoonEyeState eyeState;

static bool needFullRedraw = true;
static int prevIcx = EYE_CX, prevIcy = EYE_CY;
static float prevBlink = 1.0f;

// Rotary interactive control
static float cuteTargetGx = 0.0f;
static float minionTargetGx = 0.0f;
static unsigned long lastManualTurnTime = 0;

// -----------------------------------------------------------------------------
// Random & Minimum-Jerk Easing
// -----------------------------------------------------------------------------
static float frand(float a, float b) {
  return a + (b - a) * ((float)esp_random() / 4294967296.0f);
}

static inline float quint(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// -----------------------------------------------------------------------------
// Autonomous Gaze Engine (Natural Left/Right Looks)
// -----------------------------------------------------------------------------
static struct {
  float fx = 0, fy = 0;          // Start gaze
  float tx = 0, ty = 0;          // Target gaze
  float cx = 0, cy = 0;          // Current smoothed gaze
  uint32_t t0 = 0, dur = 1;      // Saccade timing
  uint32_t nextMove = 1200;      // Timestamp for next saccade
  bool moving = false;
  float mx = 0, my = 0;          // Organic micro-jitter
  float mtx = 0, mty = 0;
  uint32_t nextMicro = 300;
  int sinceCentre = 0;
} Gaze;

static void startLook(uint32_t now) {
  Gaze.fx = Gaze.cx;
  Gaze.fy = Gaze.cy;
  bool away = fabsf(Gaze.cx) > 8.0f || fabsf(Gaze.cy) > 4.0f;

  if (Gaze.sinceCentre >= 2 || (away && frand(0, 1) < 0.5f)) {
    // Return to center
    Gaze.tx = frand(-3.0f, 3.0f);
    Gaze.ty = frand(-2.0f, 2.0f);
    Gaze.sinceCentre = 0;
  } else {
    // Look left or right
    int dir = (int)frand(0, 4);
    if (dir == 0) {
      Gaze.tx = -frand(26.0f, 42.0f);  // Look left
      Gaze.ty = frand(-3.0f, 3.0f);
    } else if (dir == 1) {
      Gaze.tx = frand(26.0f, 42.0f);   // Look right
      Gaze.ty = frand(-3.0f, 3.0f);
    } else if (dir == 2) {
      Gaze.tx = -frand(18.0f, 32.0f);  // Left angled
      Gaze.ty = frand(0, 1) < 0.5f ? -frand(4.0f, 8.0f) : frand(4.0f, 8.0f);
    } else {
      Gaze.tx = frand(18.0f, 32.0f);   // Right angled
      Gaze.ty = frand(0, 1) < 0.5f ? -frand(4.0f, 8.0f) : frand(4.0f, 8.0f);
    }
    Gaze.sinceCentre++;
  }

  float dist = hypotf(Gaze.tx - Gaze.fx, Gaze.ty - Gaze.fy);
  Gaze.dur = (uint32_t)(200 + dist * 7.0f); // Snappy, natural saccades (0.2s - 0.45s)
  Gaze.t0 = now;
  Gaze.moving = true;
}

static void updateGaze(uint32_t now, float dt) {
  if (!Gaze.moving && now >= Gaze.nextMove) {
    startLook(now);
  }
  if (Gaze.moving) {
    float t = (float)(now - Gaze.t0) / (float)Gaze.dur;
    float k = quint(t);
    Gaze.cx = Gaze.fx + (Gaze.tx - Gaze.fx) * k;
    Gaze.cy = Gaze.fy + (Gaze.ty - Gaze.fy) * k;
    if (t >= 1.0f) {
      Gaze.moving = false;
      Gaze.nextMove = now + (uint32_t)frand(1200, 3200); // Hold look for 1.2s - 3.2s
    }
  }

  // Micro-saccades (subtle organic breathing jitter)
  if (now >= Gaze.nextMicro) {
    Gaze.mtx = frand(-1.0f, 1.0f);
    Gaze.mty = frand(-0.7f, 0.7f);
    Gaze.nextMicro = now + (uint32_t)frand(250, 700);
  }
  float a = 1.0f - expf(-dt * 14.0f);
  Gaze.mx += (Gaze.mtx - Gaze.mx) * a;
  Gaze.my += (Gaze.mty - Gaze.my) * a;
}

// -----------------------------------------------------------------------------
// Ultra-Fast Snappy Blinking Engine (70 ms Total Blink!)
// -----------------------------------------------------------------------------
enum { BLINK_IDLE, BLINK_CLOSE, BLINK_HOLD, BLINK_OPEN };
static struct {
  uint8_t phase = BLINK_IDLE;
  uint32_t t0 = 0;
  uint32_t next = 2500;
  bool dbl = false;
} Blink;

// Snappy cartoon blink: 25ms close, 10ms hold, 35ms open = 70ms total!
static const uint16_t T_CLOSE = 25, T_HOLD = 10, T_OPEN = 35;

static float updateBlink(uint32_t now) {
  switch (Blink.phase) {
    case BLINK_IDLE:
      if (now >= Blink.next) {
        Blink.phase = BLINK_CLOSE;
        Blink.t0 = now;
        Blink.dbl = frand(0, 1) < 0.20f;
      }
      return 1.0f;

    case BLINK_CLOSE: {
      float t = (float)(now - Blink.t0) / T_CLOSE;
      if (t >= 1.0f) {
        Blink.phase = BLINK_HOLD;
        Blink.t0 = now;
        return 0.0f;
      }
      return 1.0f - t * t;
    }

    case BLINK_HOLD:
      if (now - Blink.t0 >= T_HOLD) {
        Blink.phase = BLINK_OPEN;
        Blink.t0 = now;
      }
      return 0.0f;

    default: { // BLINK_OPEN
      float t = (float)(now - Blink.t0) / T_OPEN;
      if (t >= 1.0f) {
        Blink.phase = BLINK_IDLE;
        Blink.next = now + (Blink.dbl ? (uint32_t)frand(90, 160) : (uint32_t)frand(2000, 5000));
        Blink.dbl = false;
        return 1.0f;
      }
      return 1.0f - (1.0f - t) * (1.0f - t);
    }
  }
}

// -----------------------------------------------------------------------------
// Fast Band Drawing Function
// -----------------------------------------------------------------------------
static void drawBands(int y0, int y1, uint32_t now) {
  if (y0 < 0) y0 = 0;
  if (y1 > EYE_H) y1 = EYE_H;
  if (y0 >= y1) return;

  // Align to band boundary for optimal SPI DMA burst transfers
  y0 = (y0 / BAND_H) * BAND_H;
  y1 = ((y1 + BAND_H - 1) / BAND_H) * BAND_H;
  if (y1 > EYE_H) y1 = EYE_H;

  for (int y = y0; y < y1; y += BAND_H) {
    int h = y1 - y;
    if (h > BAND_H) h = BAND_H;
    renderCartoonRect(0, y, EYE_W, y + h, currentMode, eyeState, now, band);
    gfx->draw16bitRGBBitmap(0, y, band, EYE_W, h);
  }
}

// -----------------------------------------------------------------------------
// Input Handling: Direction-Based Switching & Sleep Mode
// -----------------------------------------------------------------------------
static void processInputs() {
  // 1. Process Rotary Encoder Rotation
  int delta = 0;
  noInterrupts();
  delta = encoderDelta;
  encoderDelta = 0;
  interrupts();

  if (delta != 0) {
    if (currentMode == MODE_SLEEP) {
      // WAKE UP according to rotation direction!
      if (delta > 0) {
        currentMode = MODE_CUTE;
        Serial.println("[WAKE RIGHT] -> CUTE EYE");
      } else {
        currentMode = MODE_MINION;
        Serial.println("[WAKE LEFT] -> MINION EYE");
      }
      needFullRedraw = true;
    } else {
      // Direction-based transition:
      // Turn RIGHT (CW, delta > 0) -> Cute Eyes!
      // Turn LEFT  (CCW, delta < 0)-> Minion Eyes!
      if (delta > 0 && currentMode != MODE_CUTE) {
        currentMode = MODE_CUTE;
        needFullRedraw = true;
        Serial.println("[KNOB RIGHT] Mode -> CUTE EYE");
      } else if (delta < 0 && currentMode != MODE_MINION) {
        currentMode = MODE_MINION;
        needFullRedraw = true;
        Serial.println("[KNOB LEFT] Mode -> MINION EYE");
      }
    }

    // Direct steering according to rotation
    if (currentMode == MODE_CUTE) {
      cuteTargetGx += delta * 4.5f;
      if (cuteTargetGx < -45.0f) cuteTargetGx = -45.0f;
      if (cuteTargetGx > 45.0f)  cuteTargetGx = 45.0f;
      lastManualTurnTime = millis();
    } else if (currentMode == MODE_MINION) {
      minionTargetGx += delta * 4.5f;
      if (minionTargetGx < -45.0f) minionTargetGx = -45.0f;
      if (minionTargetGx > 45.0f)  minionTargetGx = 45.0f;
      lastManualTurnTime = millis();
    }
  }

  // 2. Process Button Press (Checks both PIN_ENCODER_SW and on-board BOOT button)
  static bool lastReading = HIGH;
  static unsigned long lastDebounceTime = 0;
  static bool stableButtonState = HIGH;
  static unsigned long pressStartTime = 0;
  static bool longPressHandled = false;

  int sw1 = digitalRead(PIN_ENCODER_SW);
  int sw2 = digitalRead(PIN_BOARD_BOOT);
  // Active LOW on either pin
  int reading = (sw1 == LOW || sw2 == LOW) ? LOW : HIGH;

  if (reading != lastReading) {
    lastDebounceTime = millis();
    lastReading = reading;
  }

  if ((millis() - lastDebounceTime) > 30) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        pressStartTime = millis();
        longPressHandled = false;
      } else {
        unsigned long pressDuration = millis() - pressStartTime;
        if (!longPressHandled && pressDuration < 800) {
          // Button clicked:
          if (currentMode != MODE_SLEEP) {
            // Put eye to SLEEP!
            currentMode = MODE_SLEEP;
            needFullRedraw = true;
            Serial.println("[BUTTON] Mode -> SLEEP MODE (Z z z)");
          } else {
            // Wake up to Minion Eye
            currentMode = MODE_MINION;
            needFullRedraw = true;
            Serial.println("[BUTTON] Mode -> WOKE UP (MINION EYE)");
          }
        }
      }
    } else if (stableButtonState == LOW && !longPressHandled) {
      // Long press (> 1000 ms) in Cute mode toggles Glint Style
      if (millis() - pressStartTime > 1000) {
        longPressHandled = true;
        if (currentMode == MODE_CUTE) {
          eyeState.cuteStyle = (eyeState.cuteStyle == 1) ? 2 : 1;
          needFullRedraw = true;
          Serial.printf("[BUTTON LONG] Cute Style -> %d (%s)\n",
                        eyeState.cuteStyle,
                        eyeState.cuteStyle == 1 ? "Triple Glint" : "Twin Glint");
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Setup & Loop
// -----------------------------------------------------------------------------
static uint32_t lastFrame = 0;

void setup() {
  Serial.begin(115200);
  delay(100);

  // Setup Rotary Encoder Pins + On-board BOOT Button
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT,  INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW,  INPUT_PULLUP); // GPIO 5
  pinMode(PIN_BOARD_BOOT,  INPUT_PULLUP); // GPIO 9
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), isrEncoderCLK, FALLING);

  // Initialize GC9A01 Display Bus
  if (!gfx->begin(SPI_HZ)) {
    Serial.println("Display initialization failed!");
    while (1) delay(1000);
  }
  gfx->setRotation(0);
  gfx->fillScreen(0x0000);

  // Pre-render Iris Sprite in RAM (Zero float math during loop!)
  initCartoonEye();

  eyeState.cuteStyle = 1;
  currentMode = MODE_MINION;
  needFullRedraw = true;
  lastFrame = millis();

  Serial.println("==========================================");
  Serial.println(" Interactive 60 FPS Eye Simulation Active ");
  Serial.println(" - Turn RIGHT : CUTE EYE                  ");
  Serial.println(" - Turn LEFT  : MINION EYE (No Yellow)   ");
  Serial.println(" - Press SW   : SLEEP MODE (Z z z)        ");
  Serial.println(" - Turn either: WAKE UP in that direction ");
  Serial.println("==========================================");
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;
  lastFrame = now;

  // Process Rotary Knob and Push Button
  processInputs();

  // Animation Update
  eyeState.blink = updateBlink(now);

  if (currentMode == MODE_MINION) {
    // Minion Eye Mode:
    // If knob was turned recently (< 1.8s ago), smoothly track manual gaze
    if (now - lastManualTurnTime < 1800) {
      float followSpeed = 1.0f - expf(-dt * 24.0f);
      eyeState.gx += (minionTargetGx - eyeState.gx) * followSpeed;
      eyeState.gy = 0.0f;
      eyeState.mx = 0.0f;
      eyeState.my = 0.0f;
    } else {
      // Autonomous natural looks
      updateGaze(now, dt);
      eyeState.gx = Gaze.cx;
      eyeState.gy = Gaze.cy;
      eyeState.mx = Gaze.mx;
      eyeState.my = Gaze.my;
      minionTargetGx = eyeState.gx;
    }
  } else if (currentMode == MODE_CUTE) {
    // Cute Eye Mode:
    if (now - lastManualTurnTime < 1800) {
      float followSpeed = 1.0f - expf(-dt * 24.0f);
      eyeState.gx += (cuteTargetGx - eyeState.gx) * followSpeed;
      eyeState.gy = 0.0f;
      eyeState.mx = 0.0f;
      eyeState.my = 0.0f;
    } else {
      // Gentle cute autonomous looks
      updateGaze(now, dt);
      eyeState.gx = Gaze.cx * 0.70f;
      eyeState.gy = Gaze.cy * 0.35f;
      eyeState.mx = Gaze.mx * 0.4f;
      eyeState.my = Gaze.my * 0.4f;
      cuteTargetGx = eyeState.gx;
    }
  }

  // Calculate current iris center
  int icx = EYE_CX + (int)lroundf(eyeState.gx + eyeState.mx);
  int icy = EYE_CY + (int)lroundf(eyeState.gy + eyeState.my);

  // Redraw logic
  if (needFullRedraw) {
    drawBands(0, EYE_H, now);
    needFullRedraw = false;
    prevIcx = icx;
    prevIcy = icy;
    prevBlink = eyeState.blink;
  } else if (currentMode == MODE_SLEEP) {
    // Redraw rows 15 to 118 where the 3 cartoon Z's float up, grow, and fade away
    drawBands(15, 118, now);
  } else {
    // Dirty Band Optimization for Minion and Cute modes
    int y_min = EYE_H, y_max = 0;

    int r_bound = (currentMode == MODE_MINION) ? 48 : 94;
    if (icx != prevIcx || icy != prevIcy) {
      int top = (icy < prevIcy ? icy : prevIcy) - r_bound;
      int bot = (icy > prevIcy ? icy : prevIcy) + r_bound;
      if (top < y_min) y_min = top;
      if (bot > y_max) y_max = bot;
      prevIcx = icx;
      prevIcy = icy;
    }

    if (fabsf(eyeState.blink - prevBlink) > 0.005f) {
      int top = (currentMode == MODE_MINION) ? 18 : 10;
      int bot = (currentMode == MODE_MINION) ? 140 : 230;
      if (top < y_min) y_min = top;
      if (bot > y_max) y_max = bot;
      prevBlink = eyeState.blink;
    }

    if (y_min < y_max) {
      drawBands(y_min, y_max, now);
    }
  }

#if DEBUG_FPS
  static uint32_t t_fps = 0, frames = 0;
  frames++;
  if (now - t_fps >= 2000) {
    const char *modeStr = (currentMode == MODE_MINION) ? "MINION" :
                          (currentMode == MODE_CUTE)   ? "CUTE" : "SLEEP (Zzz)";
    Serial.printf("%.1f fps, Free Heap: %u bytes, Mode: %s\n",
                  frames * 1000.0f / (now - t_fps),
                  ESP.getFreeHeap(),
                  modeStr);
    t_fps = now;
    frames = 0;
  }
#endif
}
