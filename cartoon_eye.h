// cartoon_eye.h - High-Performance Scanline Renderer (60+ FPS)
// Modes: Minion Eye (No Yellow), Cute Eye (Image 1 & 2), Sleep Mode (Animated Floating Zzz)
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "sleep_eye.h"

#define EYE_W 240
#define EYE_H 240
#define EYE_CX 120
#define EYE_CY 120

enum EyeMode {
  MODE_MINION = 0,
  MODE_CUTE   = 1,
  MODE_SLEEP  = 2
};

struct CartoonEyeState {
  float gx = 0.0f;       // Horizontal gaze offset in px (-45 to +45)
  float gy = 0.0f;       // Vertical gaze offset in px (-15 to +15)
  float mx = 0.0f;       // Micro-saccade jitter X
  float my = 0.0f;       // Micro-saccade jitter Y
  float blink = 1.0f;    // 1.0 = fully open, 0.0 = fully closed
  uint8_t cuteStyle = 1; // 1 = Triple Glint, 2 = Twin Glint
};

// -----------------------------------------------------------------------------
// Fast Bitwise Integer Square Root (12 cycles, 100% accurate for 0..14400)
// -----------------------------------------------------------------------------
static inline uint16_t isqrt(uint32_t val) {
  if (val == 0) return 0;
  uint32_t b = 1U << 14;
  uint32_t q = 0;
  uint32_t r = val;
  while (b > 0) {
    if (r >= q + b) {
      r -= q + b;
      q = (q >> 1) + b;
    } else {
      q >>= 1;
    }
    b >>= 2;
  }
  return (uint16_t)q;
}

// -----------------------------------------------------------------------------
// Color Constants & Fast RGB565 Blending
// -----------------------------------------------------------------------------
static inline constexpr uint16_t to565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 0) return bg;
  if (a >= 255) return fg;
  uint32_t rb = (bg >> 11) & 0x1F;
  uint32_t gb = (bg >> 5)  & 0x3F;
  uint32_t bb = bg         & 0x1F;
  uint32_t rf = (fg >> 11) & 0x1F;
  uint32_t gf = (fg >> 5)  & 0x3F;
  uint32_t bf = fg         & 0x1F;
  uint32_t r = rb + (((rf - rb) * a) >> 8);
  uint32_t g = gb + (((gf - gb) * a) >> 8);
  uint32_t b = bb + (((bf - bb) * a) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static const uint16_t COL_BLACK         = to565(12, 10, 15);
static const uint16_t COL_WHITE         = to565(255, 255, 255);
static const uint16_t COL_SCLERA        = to565(252, 252, 250);
static const uint16_t COL_GOGGLE        = to565(175, 175, 180);
static const uint16_t COL_GOGGLE_DARK   = to565(85, 85, 92);
static const uint16_t COL_IRIS_LIMBAL   = to565(60, 28, 14);
static const uint16_t COL_IRIS_WARM     = to565(198, 122, 36);
static const uint16_t COL_LASH_DARK     = to565(35, 35, 45);

// Pre-rendered 90x90 Minion Iris Texture in RAM (Zero float math during rendering)
static uint16_t minionIrisTex[90 * 90];

// -----------------------------------------------------------------------------
// Pre-render Minion Iris Texture (Runs ONCE at boot in ~1.5ms)
// -----------------------------------------------------------------------------
static void initCartoonEye() {
  const int d = 45;
  for (int y = 0; y < 90; y++) {
    for (int x = 0; x < 90; x++) {
      int dx = x - d;
      int dy = y - d;
      int d2 = dx * dx + dy * dy;
      if (d2 <= 45 * 45) {
        int dist = isqrt(d2);
        // Specular highlight: white dot in upper-left (-9, -9), radius 6
        int g_d2 = (dx + 9) * (dx + 9) + (dy + 9) * (dy + 9);
        if (g_d2 <= 6 * 6) {
          minionIrisTex[y * 90 + x] = COL_WHITE;
        } else if (dist <= 17) {
          minionIrisTex[y * 90 + x] = COL_BLACK;
        } else if (dist >= 41) {
          minionIrisTex[y * 90 + x] = COL_IRIS_LIMBAL;
        } else if (dist <= 24) {
          minionIrisTex[y * 90 + x] = COL_IRIS_WARM;
        } else {
          // Warm radial amber spokes
          float ang = atan2f((float)dy, (float)dx);
          int spoke = (int)(12.0f * sinf(ang * 16.0f));
          minionIrisTex[y * 90 + x] = to565((uint8_t)(155 + spoke), (uint8_t)(85 + spoke / 2), 25);
        }
      } else {
        minionIrisTex[y * 90 + x] = 0x0000; // Transparent key
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Fast Minion Eye Row Renderer (NO YELLOW - Clean white sclera inside goggle)
// -----------------------------------------------------------------------------
static inline void renderMinionRow(int y, int x0, int x1, const CartoonEyeState &s, uint16_t *row) {
  int dy_screen = y - EYE_CY;
  int rem_120 = 120 * 120 - dy_screen * dy_screen;
  if (rem_120 <= 0) {
    memset(row, 0, (x1 - x0) * sizeof(uint16_t));
    return;
  }
  int hw_120 = isqrt(rem_120);
  int sx0 = EYE_CX - hw_120;
  int sx1 = EYE_CX + hw_120;

  int rem_106 = 106 * 106 - dy_screen * dy_screen;
  int hw_106 = (rem_106 > 0) ? isqrt(rem_106) : 0;
  int gx0 = EYE_CX - hw_106;
  int gx1 = EYE_CX + hw_106;

  // 1. Outside display: black
  // 2. Goggle Rim: metallic silver between hw_106 and hw_120
  // 3. Inside Goggle: PURE WHITE SCLERA (No yellow!)
  for (int x = x0; x < x1; x++) {
    if (x < sx0 || x >= sx1) {
      row[x - x0] = 0x0000;
    } else if (x < gx0 || x >= gx1) {
      row[x - x0] = (x > sx0 + 2 && x < sx1 - 2) ? COL_GOGGLE : COL_GOGGLE_DARK;
    } else {
      row[x - x0] = COL_SCLERA;
    }
  }

  if (hw_106 <= 0) return;

  int kx0 = (gx0 > x0) ? gx0 : x0;
  int kx1 = (gx1 < x1) ? gx1 : x1;

  // Snappy Fast Cartoon Blink (No yellow)
  if (s.blink < 0.99f) {
    int y_close = 120;
    int y_upper = (int)(20.0f + 100.0f * (1.0f - s.blink));
    if (y < y_upper) {
      for (int x = kx0; x < kx1; x++) row[x - x0] = COL_SCLERA;
      if (y >= y_upper - 2) {
        for (int x = kx0; x < kx1; x++) row[x - x0] = COL_LASH_DARK;
      }
      return;
    }
  }

  // Soft upper eyelid shadow
  if (y >= 20 && y < 32) {
    uint16_t sh_col = (y < 26) ? to565(225, 225, 222) : to565(240, 240, 238);
    for (int x = kx0; x < kx1; x++) row[x - x0] = sh_col;
  }

  // Iris overlay from pre-rendered sprite
  int icx = EYE_CX + (int)s.gx + (int)s.mx;
  int icy = EYE_CY + (int)s.gy + (int)s.my;
  if (y >= icy - 45 && y < icy + 45) {
    int iris_row = y - (icy - 45);
    int iris_x0 = icx - 45;
    const uint16_t *src = &minionIrisTex[iris_row * 90];
    for (int ix = 0; ix < 90; ix++) {
      int px = iris_x0 + ix;
      if (px >= kx0 && px < kx1) {
        uint16_t c = src[ix];
        if (c != 0) {
          row[px - x0] = c;
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Fast Cute Eye Row Renderer (Span-based, 60+ FPS)
// -----------------------------------------------------------------------------
static inline void renderCuteRow(int y, int x0, int x1, const CartoonEyeState &s, uint16_t *row) {
  int dy_screen = y - EYE_CY;
  int rem_screen = 120 * 120 - dy_screen * dy_screen;
  if (rem_screen <= 0) {
    memset(row, 0, (x1 - x0) * sizeof(uint16_t));
    return;
  }
  int hw_screen = isqrt(rem_screen);
  int sx0 = EYE_CX - hw_screen;
  int sx1 = EYE_CX + hw_screen;

  // Initialize row with white inside circle, black outside circle
  for (int x = x0; x < x1; x++) {
    row[x - x0] = (x >= sx0 && x < sx1) ? COL_WHITE : 0x0000;
  }

  // Handle blinking
  if (s.blink < 0.99f) {
    int y_upper = (int)(10.0f + 110.0f * (1.0f - s.blink));
    int y_lower = (int)(230.0f - 110.0f * (1.0f - s.blink));
    if (y < y_upper || y > y_lower) {
      if (s.blink < 0.25f) {
        int arch = (hw_screen > 0) ? (18 * hw_screen) / 120 : 0;
        int arc_y = 120 - arch;
        if (abs(y - arc_y) <= 2) {
          int kx0 = (sx0 > x0) ? sx0 : x0;
          int kx1 = (sx1 < x1) ? sx1 : x1;
          for (int x = kx0; x < kx1; x++) row[x - x0] = COL_LASH_DARK;
        }
      }
      return;
    }
  }

  // Large Cute Pupil (Radius 92 px)
  int icx = EYE_CX + (int)s.gx + (int)s.mx;
  int icy = EYE_CY + (int)s.gy + (int)s.my;
  int dy_iris = y - icy;
  const int R_iris = 92;
  int rem_iris = R_iris * R_iris - dy_iris * dy_iris;
  if (rem_iris > 0) {
    int hw_iris = isqrt(rem_iris);
    int ix0 = icx - hw_iris;
    int ix1 = icx + hw_iris;
    if (ix0 < sx0) ix0 = sx0;
    if (ix1 > sx1) ix1 = sx1;
    if (ix0 < x0) ix0 = x0;
    if (ix1 > x1) ix1 = x1;

    for (int x = ix0; x < ix1; x++) {
      row[x - x0] = COL_BLACK;
    }

    // High-contrast shiny white glints
    if (s.cuteStyle == 1) {
      // Style 1 (Image 1): Triple Glint
      // Glint 1: Top-Left (-34, -26), r=37
      int g1x = icx - 34, g1y = icy - 26;
      int rem_g1 = 37 * 37 - (y - g1y) * (y - g1y);
      if (rem_g1 > 0) {
        int hw = isqrt(rem_g1);
        int gx0 = g1x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g1x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
      // Glint 2: Mid-Right (+33, -7), r=25
      int g2x = icx + 33, g2y = icy - 7;
      int rem_g2 = 25 * 25 - (y - g2y) * (y - g2y);
      if (rem_g2 > 0) {
        int hw = isqrt(rem_g2);
        int gx0 = g2x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g2x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
      // Glint 3: Bottom-Right (+16, +34), r=21
      int g3x = icx + 16, g3y = icy + 34;
      int rem_g3 = 21 * 21 - (y - g3y) * (y - g3y);
      if (rem_g3 > 0) {
        int hw = isqrt(rem_g3);
        int gx0 = g3x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g3x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
    } else {
      // Style 2 (Image 2): Twin Glint
      // Glint 1: Top-Right (+28, -36), r=20
      int g1x = icx + 28, g1y = icy - 36;
      int rem_g1 = 20 * 20 - (y - g1y) * (y - g1y);
      if (rem_g1 > 0) {
        int hw = isqrt(rem_g1);
        int gx0 = g1x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g1x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
      // Glint 2: Bottom-Left 1 (-36, +24), r=15
      int g2x = icx - 36, g2y = icy + 24;
      int rem_g2 = 15 * 15 - (y - g2y) * (y - g2y);
      if (rem_g2 > 0) {
        int hw = isqrt(rem_g2);
        int gx0 = g2x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g2x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
      // Glint 3: Bottom-Left 2 (-22, +40), r=15
      int g3x = icx - 22, g3y = icy + 40;
      int rem_g3 = 15 * 15 - (y - g3y) * (y - g3y);
      if (rem_g3 > 0) {
        int hw = isqrt(rem_g3);
        int gx0 = g3x - hw; if (gx0 < ix0) gx0 = ix0;
        int gx1 = g3x + hw; if (gx1 > ix1) gx1 = ix1;
        for (int x = gx0; x < gx1; x++) row[x - x0] = COL_WHITE;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Cartoon "Z" Span-Drawing Helper
// -----------------------------------------------------------------------------
static inline void drawZRow(int y, int zx, int zy, int size, uint16_t col,
                            int gx0, int gx1, int x0, int x1, uint16_t *row) {
  int w = size;
  int h = (size * 29) / 25;   // ~1.16 aspect ratio
  int th = (size * 22) / 100; // Stroke thickness
  if (th < 2) th = 2;

  int y_top = zy - h / 2;
  int y_bot = y_top + h;

  if (y < y_top || y >= y_bot) return;

  int x_left = zx - w / 2;
  int x_right = x_left + w;

  if (y < y_top + th) {
    // Top horizontal bar
    int kx0 = (x_left > x0) ? x_left : x0; if (kx0 < gx0) kx0 = gx0;
    int kx1 = (x_right < x1) ? x_right : x1; if (kx1 > gx1) kx1 = gx1;
    for (int x = kx0; x < kx1; x++) row[x - x0] = col;
  } else if (y >= y_bot - th) {
    // Bottom horizontal bar
    int kx0 = (x_left > x0) ? x_left : x0; if (kx0 < gx0) kx0 = gx0;
    int kx1 = (x_right < x1) ? x_right : x1; if (kx1 > gx1) kx1 = gx1;
    for (int x = kx0; x < kx1; x++) row[x - x0] = col;
  } else {
    // Diagonal bar connecting top-right to bottom-left
    int dy = y - y_top;
    int x_diag = x_right - (dy * w) / h;
    int kx0 = x_diag - th / 2; if (kx0 < x0) kx0 = x0; if (kx0 < gx0) kx0 = gx0;
    int kx1 = x_diag + (th + 1) / 2; if (kx1 > x1) kx1 = x1; if (kx1 > gx1) kx1 = gx1;
    for (int x = kx0; x < kx1; x++) row[x - x0] = col;
  }
}

// -----------------------------------------------------------------------------
// Sleep Mode Row Renderer:
// - Lower Half: User's exact sleeping eye arc with 5 eyelashes from 1-bit bitmap
// - Upper Half: 3 cartoon "Z"s emerging from the eye, floating up & fading away!
// -----------------------------------------------------------------------------
static inline void renderSleepRow(int y, int x0, int x1, const CartoonEyeState &s,
                                  uint32_t now, uint16_t *row) {
  int dy_screen = y - EYE_CY;
  int rem_120 = 120 * 120 - dy_screen * dy_screen;
  if (rem_120 <= 0) {
    memset(row, 0, (x1 - x0) * sizeof(uint16_t));
    return;
  }
  int hw_120 = isqrt(rem_120);
  int sx0 = EYE_CX - hw_120;
  int sx1 = EYE_CX + hw_120;

  int rem_106 = 106 * 106 - dy_screen * dy_screen;
  int hw_106 = (rem_106 > 0) ? isqrt(rem_106) : 0;
  int gx0 = EYE_CX - hw_106;
  int gx1 = EYE_CX + hw_106;

  // Background: Goggle rim + pure white interior
  for (int x = x0; x < x1; x++) {
    if (x < sx0 || x >= sx1) {
      row[x - x0] = 0x0000;
    } else if (x < gx0 || x >= gx1) {
      row[x - x0] = (x > sx0 + 2 && x < sx1 - 2) ? COL_GOGGLE : COL_GOGGLE_DARK;
    } else {
      row[x - x0] = COL_SCLERA;
    }
  }

  if (hw_106 <= 0) return;

  // 1. Overlay Sleeping Eye Arc with 5 Eyelashes (Bottom half, rows 120 to 190)
  if (y >= SLEEP_EYE_Y0 && y < SLEEP_EYE_Y0 + SLEEP_EYE_H) {
    int art_y = y - SLEEP_EYE_Y0;
    const uint8_t *row_bytes = &SLEEP_EYE_BITMAP[art_y * SLEEP_EYE_ROW_BYTES];
    for (int b = 0; b < SLEEP_EYE_ROW_BYTES; b++) {
      uint8_t byte_val = pgm_read_byte(&row_bytes[b]);
      if (byte_val == 0) continue;
      int base_x = SLEEP_EYE_X0 + b * 8;
      for (int bit = 0; bit < 8; bit++) {
        if ((byte_val >> (7 - bit)) & 1) {
          int px = base_x + bit;
          if (px >= x0 && px < x1 && px >= gx0 && px < gx1) {
            row[px - x0] = COL_BLACK;
          }
        }
      }
    }
  }

  // 2. Animated Floating Cartoon "Z z z" (Upper half, rows 15 to 118)
  // Continuous 2.4-second looping animation
  uint32_t cycle = now % 2400;
  float cycle_t = (float)cycle / 2400.0f;

  for (int i = 0; i < 3; i++) {
    float t = cycle_t + (float)i / 3.0f;
    if (t >= 1.0f) t -= 1.0f;

    // Organic S-curve sway as Z floats upward
    float sway = 7.0f * sinf(t * 6.283185f);
    int zx = (int)(106.0f + 64.0f * t + sway);
    int zy = (int)(108.0f - 80.0f * t);
    int size = (int)(11.0f + 14.0f * t); // Grows from 11px to 25px

    // Fades into white as it reaches the top
    uint16_t col = COL_BLACK;
    if (t > 0.70f) {
      uint8_t fade = (uint8_t)(((t - 0.70f) / 0.30f) * 255.0f);
      col = blend565(COL_BLACK, COL_WHITE, fade);
    }

    if (t < 0.98f) {
      drawZRow(y, zx, zy, size, col, gx0, gx1, x0, x1, row);
    }
  }
}

// -----------------------------------------------------------------------------
// Fast Band Renderer Dispatcher
// -----------------------------------------------------------------------------
static inline void renderCartoonRect(int x0, int y0, int x1, int y1,
                                     EyeMode mode, const CartoonEyeState &s,
                                     uint32_t now, uint16_t *buf) {
  int w = x1 - x0;
  if (mode == MODE_MINION) {
    for (int y = y0; y < y1; y++) {
      renderMinionRow(y, x0, x1, s, buf);
      buf += w;
    }
  } else if (mode == MODE_CUTE) {
    for (int y = y0; y < y1; y++) {
      renderCuteRow(y, x0, x1, s, buf);
      buf += w;
    }
  } else { // MODE_SLEEP
    for (int y = y0; y < y1; y++) {
      renderSleepRow(y, x0, x1, s, now, buf);
      buf += w;
    }
  }
}
