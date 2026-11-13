// eye_render.h - photographic eye compositor (plain C++, no Arduino dependencies)
//
// Layers, bottom to top, evaluated per pixel:
//   skin texture -> sclera texture (veins) -> iris texture (radially remapped for pupil size)
//   -> corneal gloss -> upper-lid shadow -> waterline -> lid margins -> eyelashes
// Only the rectangle that changed since the last frame is re-rendered.
#pragma once
#include <stdint.h>
#include <math.h>
#include "eye_assets.h"

struct EyeState {
  float open;        // 0 = closed, 1 = fully open
  float gx, gy;      // gaze offset in px (eyeball + sclera move with it)
  float mx, my;      // micro-movement in px (iris only)
  float pupil;       // pupil radius in px (iris texture was baked with IRIS_P0)
};
struct EyeRect { int x0, y0, x1, y1; };  // [x0,x1) x [y0,y1)

#define EYE_H EYE_W        // square display
#define SKU_N 36           // skin shading range above the upper lid (px)
#define SKL_N 22           // ... below the lower lid
#define SHE_N 24           // eyeball shadow range under the upper lid
#define LUT_N ((IRIS_R + 2) * 4 + 1)

static int16_t  YU8[EYE_W], YL8[EYE_W];     // lid edges now (Q3 = 1/8 px)
static int16_t  RU8[EYE_W], RL8[EYE_W];     // lid edges as last rendered
static float    HS[EYE_W];                  // lid profile
static uint8_t  DIST4[IRIS_S * IRIS_S];     // distance*4 from the iris centre
static uint16_t ISCALE[LUT_N];              // radial remap (8.8) for the current pupil size
static uint8_t  IALPHA[LUT_N], PALPHA[LUT_N];
static uint8_t  SH_SU[SKU_N], SH_SL[SKL_N], SH_EU[SHE_N], EDGE_SH[EYE_W], PINK_X[EYE_W];
static float    lutPupil = -1.0f;
static int      icx = EYE_CX, icy = EYE_CY, ricx = -999, ricy = -999;
static int      sclX = 0, sclY = 0, rsclX = -999, rsclY = -999;
static int      glossX = 0, glossY = 0;
static int      lashUp = 1, lashInv = 256;
static bool     needFull = true;

static const uint8_t RIM_U_A[3] = {200, 120, 50};
static const uint8_t RIM_U_C[3] = {52, 32, 28};      // dark lash line
static const uint8_t RIM_L_A[3] = {190, 110, 45};
static const uint8_t RIM_L_C[3] = {196, 122, 112};   // pink tarsal rim
static const uint8_t WL_A[8]    = {255, 235, 205, 150, 90, 80, 55, 0};
static const uint8_t WL_C[8][3] = {{168, 96, 92}, {196, 118, 112}, {214, 136, 130}, {222, 156, 150},
                                   {232, 184, 178}, {250, 244, 240}, {255, 255, 255}, {0, 0, 0}};

static inline float sstepf(float a, float b, float x) {
  float t = (x - a) / (b - a);
  if (t < 0) t = 0; if (t > 1) t = 1;
  return t * t * (3 - 2 * t);
}

static void buildPupilLUT(float p) {
  lutPupil = p;
  for (int i = 0; i < LUT_N; i++) {
    float d = i * 0.25f;
    float ia = (IRIS_R + 0.75f - d) / 1.5f;  if (ia < 0) ia = 0; if (ia > 1) ia = 1;
    float pa = (d - p) / 1.4f + 0.5f;        if (pa < 0) pa = 0; if (pa > 1) pa = 1;
    float dd = d < 0.25f ? 0.25f : d;
    float rs = IRIS_P0 + (d > p ? (d - p) : 0.0f) * (IRIS_R - IRIS_P0) / (IRIS_R - p);
    IALPHA[i] = (uint8_t)(ia * 255.f + 0.5f);
    PALPHA[i] = (uint8_t)(pa * 255.f + 0.5f);
    ISCALE[i] = (uint16_t)(rs / dd * 256.f + 0.5f);
  }
}

static void eyeInit() {
  for (int x = 0; x < EYE_W; x++) {
    float u = (x - EYE_CX) / (float)EYE_HW, q = 1.0f - u * u;
    HS[x] = q > 0 ? powf(q, EYE_SHAPE) : 0.0f;
    EDGE_SH[x] = (uint8_t)(255.f * (1.0f - 0.55f * sstepf(0.45f, 1.05f, fabsf(u))));
    float pk = (x < EYE_CX) ? 0.65f * (1.0f - sstepf(0.f, 1.f, (x - 8) / 40.f))
                            : 0.20f * sstepf(0.f, 1.f, (x - (EYE_W - 46)) / 36.f);
    PINK_X[x] = (uint8_t)(pk * 255.f);
    RU8[x] = RL8[x] = 0;
  }
  for (int i = 0; i < IRIS_S * IRIS_S; i++) {
    int dx = i % IRIS_S - IRIS_S / 2, dy = i / IRIS_S - IRIS_S / 2;
    float d = sqrtf((float)(dx * dx + dy * dy)) * 4.f + 0.5f;
    DIST4[i] = d > 255.f ? 255 : (uint8_t)d;
  }
  for (int i = 0; i < SKU_N; i++) {
    float c = expf(-powf((i - 26) / 3.6f, 2.f));
    SH_SU[i] = (uint8_t)(255.f * (1.f - 0.22f * c - 0.10f * expf(-i / 5.f)));
  }
  for (int i = 0; i < SKL_N; i++) {
    float t = expf(-powf((i - 14) / 4.f, 2.f));
    SH_SL[i] = (uint8_t)(255.f * (1.f - 0.10f * expf(-i / 7.f) - 0.08f * t));
  }
  for (int i = 0; i < SHE_N; i++) SH_EU[i] = (uint8_t)(255.f * (0.40f + 0.60f * sstepf(0, 21, (float)i)));
  buildPupilLUT(19.0f);
  needFull = true;
}

static inline void c565(uint16_t c, int &r, int &g, int &b) {
  r = (c >> 8) & 0xF8; g = (c >> 3) & 0xFC; b = (c << 3) & 0xF8;
  r |= r >> 5; g |= g >> 6; b |= b >> 5;
}

// ---------------------------------------------------------------------------------------------
static inline void skinPix(int x, int y, int dq, int dl, int &r, int &g, int &b) {
  c565(SKIN[y * EYE_W + x], r, g, b);
  if (dq < dl) {                                   // nearer the upper lid
    int du = dq < 0 ? (-dq) >> 3 : 0;
    if (du < SKU_N) { int m = SH_SU[du]; r = (r * m) >> 8; g = (g * m) >> 8; b = (b * m) >> 8; }
    if (du < 3) { int a = RIM_U_A[du]; r += ((RIM_U_C[0] - r) * a) >> 8; g += ((RIM_U_C[1] - g) * a) >> 8; b += ((RIM_U_C[2] - b) * a) >> 8; }
  } else {                                         // nearer the lower lid
    int dd = dl < 0 ? (-dl) >> 3 : 0;
    if (dd < SKL_N) { int m = SH_SL[dd]; r = (r * m) >> 8; g = (g * m) >> 8; b = (b * m) >> 8; }
    if (dd < 3) { int a = RIM_L_A[dd]; r += ((RIM_L_C[0] - r) * a) >> 8; g += ((RIM_L_C[1] - g) * a) >> 8; b += ((RIM_L_C[2] - b) * a) >> 8; }
  }
}

static inline void eyePix(int x, int y, int dq, int dl, int &r, int &g, int &b) {
  int tx = x + SCL_X0 - sclX, ty = y - SCL_Y0 - sclY;
  if (tx < 0) tx = 0; if (tx >= SCL_W) tx = SCL_W - 1;
  if (ty < 0) ty = 0; if (ty >= SCL_H) ty = SCL_H - 1;
  c565(SCLERA[ty * SCL_W + tx], r, g, b);

  int dx = x - icx, dy = y - icy;
  if (dx >= -IRIS_R - 1 && dx <= IRIS_R + 1 && dy >= -IRIS_R - 1 && dy <= IRIS_R + 1) {
    int d4 = DIST4[(dy + IRIS_S / 2) * IRIS_S + dx + IRIS_S / 2];
    if (d4 <= (IRIS_R + 1) * 4) {
      int ia = IALPHA[d4];
      if (ia) {
        int sc = ISCALE[d4];
        int sx = IRIS_S / 2 + (dx * sc) / 256, sy = IRIS_S / 2 + (dy * sc) / 256;
        if (sx < 0) sx = 0; if (sx > IRIS_S - 1) sx = IRIS_S - 1;
        if (sy < 0) sy = 0; if (sy > IRIS_S - 1) sy = IRIS_S - 1;
        int ir, ig, ib;
        c565(IRIS[sy * IRIS_S + sx], ir, ig, ib);
        int pa = PALPHA[d4];
        if (pa < 255) { int k = 255 - pa; ir = (ir * pa + 6 * k) >> 8; ig = (ig * pa + 5 * k) >> 8; ib = (ib * pa + 5 * k) >> 8; }
        int gxs = dx + IRIS_S / 2 + glossX, gys = dy + IRIS_S / 2 + glossY;
        if ((unsigned)gxs < IRIS_S && (unsigned)gys < IRIS_S) {
          int gl = GLOSS[gys * IRIS_S + gxs];
          if (gl) { ir += ((255 - ir) * gl) >> 8; ig += ((255 - ig) * gl) >> 8; ib += ((255 - ib) * gl) >> 8; }
        }
        r += ((ir - r) * ia) >> 8; g += ((ig - g) * ia) >> 8; b += ((ib - b) * ia) >> 8;
      }
    }
  }
  int m = EDGE_SH[x];                                  // eyeball curvature falloff towards the corners
  r = (r * m) >> 8; g = (g * m) >> 8; b = (b * m) >> 8;
  int pk = PINK_X[x];                                  // caruncle / inner corner tint
  if (pk) { r += ((200 - r) * pk) >> 8; g += ((112 - g) * pk) >> 8; b += ((108 - b) * pk) >> 8; }
  int du = dq > 0 ? dq >> 3 : 0;                       // shadow cast by the upper lid
  if (du < SHE_N) { int s = SH_EU[du]; r = (r * s) >> 8; g = (g * s) >> 8; b = (b * s) >> 8; }
  int dd = dl > 0 ? dl >> 3 : 0;                       // moist waterline + meniscus highlight
  if (dd < 8) {
    int a = WL_A[dd];
    r += ((WL_C[dd][0] - r) * a) >> 8; g += ((WL_C[dd][1] - g) * a) >> 8; b += ((WL_C[dd][2] - b) * a) >> 8;
  }
}

static inline uint16_t eyePixel(int x, int y) {
  int y8 = y * 8 + 4;
  int dq = y8 - YU8[x];            // + below the upper lid edge
  int dl = YL8[x] - y8;            // + above the lower lid edge
  int a = (dq < dl ? dq : dl) + 8; // eye-opening coverage, 0..16
  int r, g, b;
  if (a <= 0) skinPix(x, y, dq, dl, r, g, b);
  else if (a >= 16) eyePix(x, y, dq, dl, r, g, b);
  else {
    int r2, g2, b2;
    skinPix(x, y, dq, dl, r, g, b);
    eyePix(x, y, dq, dl, r2, g2, b2);
    r += ((r2 - r) * a) >> 4; g += ((g2 - g) * a) >> 4; b += ((b2 - b) * a) >> 4;
  }
  // upper lashes: point up when open, fold down (and foreshorten) as the lid closes
  int dist = lashUp ? -dq : dq;
  if (dist > -8) {
    if (dist < 0) dist = 0;
    int row = (dist * lashInv) >> 11;
    if (row < LASH_UH) {
      int la = LASH_U[row * EYE_W + x];
      if (la) { r += ((22 - r) * la) >> 8; g += ((15 - g) * la) >> 8; b += ((13 - b) * la) >> 8; }
    }
  }
  int dd8 = -dl;                    // short lower lashes
  if (dd8 > -8) {
    if (dd8 < 0) dd8 = 0;
    int row = dd8 >> 3;
    if (row < LASH_LH) {
      int la = LASH_L[row * EYE_W + x];
      if (la) { r += ((30 - r) * la) >> 8; g += ((22 - g) * la) >> 8; b += ((19 - b) * la) >> 8; }
    }
  }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void eyeRenderRect(int x0, int y0, int x1, int y1, uint16_t *buf) {
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) *buf++ = eyePixel(x, y);
}

// ---------------------------------------------------------------------------------------------
// Computes lid curves / iris position for this state and the rectangle that must be redrawn.
// Returns false when nothing visible changed.
static bool eyePrepare(const EyeState &s, EyeRect &out) {
  float c = 1.0f - s.open; if (c < 0) c = 0; if (c > 1) c = 1;
  float lidDy = (s.gy > 0 ? 0.55f : 0.30f) * s.gy;    // upper lid follows vertical gaze
  float lowDy = 0.18f * s.gy;
  for (int x = 0; x < EYE_W; x++) {
    float h = HS[x];
    float yUo = EYE_CY - EYE_AU * h + lidDy * h;
    float yLo = EYE_CY + EYE_AL * h + lowDy * h;
    float yCl = EYE_CY + (EYE_AL - EYE_CLOSE_RISE) * h;        // closed lid line sags a little at the centre
    YU8[x] = (int16_t)((yUo + (yCl + 1.0f - yUo) * c) * 8.f);  // lids overlap by 2 px when closed
    YL8[x] = (int16_t)((yLo + (yCl - 1.0f - yLo) * c) * 8.f);
  }
  float k = (s.open - 0.5f) * 2.f, ak = fabsf(k);
  if (ak < 0.2f) ak = 0.2f;
  lashUp = k >= 0; lashInv = (int)(256.f / ak);

  icx = EYE_CX + (int)lroundf(s.gx + s.mx);
  icy = EYE_CY + (int)lroundf(s.gy + s.my);
  sclX = (int)lroundf(s.gx * 0.85f); sclY = (int)lroundf(s.gy * 0.85f);
  glossX = ((icx - EYE_CX) * 13) / 20; glossY = ((icy - EYE_CY) * 13) / 20;
  bool pupilChanged = fabsf(s.pupil - lutPupil) >= 0.1f;
  if (pupilChanged) buildPupilLUT(s.pupil);

  int x0 = EYE_W, y0 = EYE_H, x1 = 0, y1 = 0;
  auto add = [&](int ax0, int ay0, int ax1, int ay1) {
    if (ax0 < 0) ax0 = 0; if (ay0 < 0) ay0 = 0; if (ax1 > EYE_W) ax1 = EYE_W; if (ay1 > EYE_H) ay1 = EYE_H;
    if (ax0 >= ax1 || ay0 >= ay1) return;
    if (ax0 < x0) x0 = ax0; if (ay0 < y0) y0 = ay0; if (ax1 > x1) x1 = ax1; if (ay1 > y1) y1 = ay1;
  };
  if (needFull) { add(0, 0, EYE_W, EYE_H); needFull = false; rsclX = sclX; rsclY = sclY; ricx = icx; ricy = icy; }
  for (int x = 0; x < EYE_W; x++) {                       // lids moved >= 1 px in this column?
    int du = YU8[x] - RU8[x], dl = YL8[x] - RL8[x];
    if (du < 0) du = -du; if (dl < 0) dl = -dl;
    if (du >= 8 || dl >= 8) {
      int ua = (YU8[x] < RU8[x] ? YU8[x] : RU8[x]) >> 3, ub = (YU8[x] > RU8[x] ? YU8[x] : RU8[x]) >> 3;
      int la = (YL8[x] < RL8[x] ? YL8[x] : RL8[x]) >> 3, lb = (YL8[x] > RL8[x] ? YL8[x] : RL8[x]) >> 3;
      add(x, ua - SKU_N, x + 1, ub + 30);
      add(x, la - 10, x + 1, lb + SKL_N);
      RU8[x] = YU8[x]; RL8[x] = YL8[x];
    }
  }
  if (sclX != rsclX || sclY != rsclY) {                   // eyeball turned: veins slide across the aperture
    int top = 999, bot = -999;
    for (int x = 0; x < EYE_W; x++) {
      int a = (YU8[x] < RU8[x] ? YU8[x] : RU8[x]) >> 3, b = (YL8[x] > RL8[x] ? YL8[x] : RL8[x]) >> 3;
      if (a < top) top = a; if (b > bot) bot = b;
    }
    add(EYE_CX - EYE_HW - 2, top - 2, EYE_CX + EYE_HW + 2, bot + 3);
    rsclX = sclX; rsclY = sclY;
  }
  if (icx != ricx || icy != ricy || pupilChanged) {       // iris / pupil / reflections
    int m = IRIS_R + 4;
    add(icx - m, icy - m, icx + m + 1, icy + m + 1);
    if (ricx > -900) add(ricx - m, ricy - m, ricx + m + 1, ricy + m + 1);
    ricx = icx; ricy = icy;
  }
  if (x0 >= x1 || y0 >= y1) return false;
  out.x0 = x0; out.y0 = y0; out.x1 = x1; out.y1 = y1;
  return true;
}
