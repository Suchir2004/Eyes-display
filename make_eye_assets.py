#!/usr/bin/env python3
"""
make_eye_assets.py  -  builds eye_assets.h for RealisticEye.ino

Needs:  pip install numpy pillow

Usage
  python make_eye_assets.py                       # procedural blue eye, light skin
  python make_eye_assets.py --iris green --skin tan
  python make_eye_assets.py --iris-photo my_iris.jpg      # use a REAL macro photo of an iris
  python make_eye_assets.py --use-png assets_png          # rebuild from PNGs you edited by hand

Every run also writes the textures as PNG files to ./assets_png so you can open them in
GIMP / Photoshop, retouch or replace them, and rebuild with --use-png.

Texture layout (must match eye_render.h - the numbers are written into the header):
  skin.png     240x240  whole face patch, incl. the skin that covers the eye when it is closed
  sclera.png   280x128  eyeball white with veins (screen x = tx-20, screen y = ty+56)
  iris.png      96x96   iris+pupil, centre (48,48), pupil radius 18 px, iris radius 46 px
  gloss.png     96x96   corneal reflection mask (window catch-light), 8-bit
  lash_up.png  240x26   upper lashes, row 0 = root at the lid margin, 8-bit alpha
  lash_dn.png  240x10   lower lashes, row 0 = root at the lid margin, 8-bit alpha
"""
import argparse, math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# ---------------------------------------------------------------- geometry (shared with C++)
W = 240
CX, CY = 120, 122            # centre of the open eye
HW = 110                     # half width of the eye opening
AU, AL = 42, 44              # upper / lower lid height at the centre (open)
CLOSE_RISE = 10              # how far the lower lid rises when the eye closes
SHAPE = 1.0                  # lid profile exponent, h(u) = (1-u^2)^SHAPE
IRIS_S, IRIS_R, IRIS_P0 = 96, 46, 18
SCL_W, SCL_H, SCL_X0, SCL_Y0 = 280, 128, 20, 56
LASH_UH, LASH_LH = 26, 10

SKIN_TONES = {'light': (228, 186, 162), 'medium': (214, 168, 140),
              'tan': (188, 142, 112), 'dark': (122, 84, 62)}
IRIS_PAL = {  # inner dark/light, outer dark/light, limbal ring
    'blue':  dict(i0=(120, 112, 92),  i1=(186, 176, 150), o0=(52, 92, 146),  o1=(136, 178, 214), lim=(22, 36, 60)),
    'green': dict(i0=(128, 104, 60),  i1=(196, 170, 104), o0=(58, 96, 62),   o1=(142, 176, 100), lim=(26, 40, 28)),
    'hazel': dict(i0=(120, 84, 40),   i1=(188, 146, 80),  o0=(92, 88, 52),   o1=(150, 140, 84),  lim=(42, 34, 22)),
    'brown': dict(i0=(92, 58, 28),    i1=(148, 104, 58),  o0=(60, 38, 22),   o1=(118, 78, 42),   lim=(20, 13, 9)),
}

# ---------------------------------------------------------------- helpers
def blur(a, sy, sx):
    """circular gaussian blur through the FFT (sigma in samples)"""
    fy = np.fft.fftfreq(a.shape[0])[:, None]
    fx = np.fft.fftfreq(a.shape[1])[None, :]
    g = np.exp(-2 * np.pi ** 2 * ((sy * fy) ** 2 + (sx * fx) ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(a) * g))

def noise(shape, sy, sx, rng):
    n = blur(rng.standard_normal(shape), sy, sx)
    return n / (n.std() + 1e-9)

def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)

def lerp(a, b, t):
    return a + (b - a) * t

def lens(x):
    u = (x - CX) / HW
    return np.where(np.abs(u) < 1, np.clip(1 - u * u, 0, 1) ** SHAPE, 0.0)

def to_u8(a):
    return np.clip(np.rint(a), 0, 255).astype(np.uint8)

# ---------------------------------------------------------------- skin
def make_skin(rng, tone):
    base = np.array(SKIN_TONES[tone], float)
    yy, xx = np.mgrid[0:W, 0:W].astype(float)
    lum = (1 + 0.018 * noise((W, W), 30, 30, rng) + 0.014 * noise((W, W), 6, 6, rng)
           + 0.026 * noise((W, W), 0.7, 0.7, rng) + 0.012 * noise((W, W), 0.8, 7, rng))
    red = noise((W, W), 22, 22, rng)
    img = base[None, None, :] * lum[..., None]
    img *= 1 + np.stack([0.014 * red, -0.006 * red, -0.012 * red], -1)

    # key light from the upper left, slightly pinker skin close to the eye
    img *= (1.05 - 0.09 * (yy / W) - 0.04 * (xx / W))[..., None]
    band = np.exp(-((yy - CY) / 60.0) ** 2)
    img *= np.stack([1 + 0.030 * band, 1 - 0.022 * band, 1 - 0.030 * band], -1)

    # eyebrow hairs along the top edge
    S = 3
    m = Image.new('L', (W * S, W * S), 0)
    d = ImageDraw.Draw(m)
    for _ in range(170):
        x0 = rng.uniform(10, 235)
        y0 = rng.uniform(-8, 24)
        ang = math.radians(lerp(-62, -10, x0 / W) + rng.normal(0, 12))
        L = rng.uniform(14, 28)
        pts, x, y, a = [], x0, y0, ang
        for _i in range(5):
            pts.append((x * S, y * S))
            a += rng.normal(0, 0.10) + 0.05
            x += math.cos(a) * L / 4
            y += math.sin(a) * L / 4
        d.line(pts, fill=int(rng.uniform(90, 190)), width=max(1, int(S * rng.uniform(0.9, 1.4))))
    hair = np.asarray(m.resize((W, W), Image.LANCZOS), float)[..., None] / 255 * 0.42
    img = img * (1 - hair) + np.array([84, 58, 46], float) * hair

    # vignette: gives the macro-lens depth-of-field falloff
    r = np.hypot(xx - W / 2, yy - W / 2) / (W / 2)
    img *= (1 - 0.20 * sstep(0.45, 1.05, r))[..., None]
    return to_u8(img)

# ---------------------------------------------------------------- sclera
def make_sclera(rng):
    h, w = SCL_H, SCL_W
    img = np.ones((h, w, 3)) * np.array([236, 230, 222], float)
    img *= (1 + 0.012 * noise((h, w), 10, 10, rng) + 0.008 * noise((h, w), 1.2, 1.2, rng))[..., None]
    sx = (np.arange(w)[None, :] - SCL_X0 - CX) / HW
    edge = sstep(0.45, 1.1, np.abs(sx)) * np.ones((h, 1))
    img[..., 1] -= 8 * edge
    img[..., 2] -= 26 * edge                      # slight yellowing towards the corners

    S = 3
    im = Image.new('L', (w * S, h * S), 0)
    d = ImageDraw.Draw(im)
    icx, icy = (CX + SCL_X0) * S, (CY - SCL_Y0) * S

    def grow(x, y, ang, steps, wd, al, depth):
        for i in range(steps):
            to = math.atan2(icy - y, icx - x)
            diff = (to - ang + math.pi) % (2 * math.pi) - math.pi
            ang += 0.05 * diff + rng.normal(0, 0.14)
            nx, ny = x + math.cos(ang) * 1.6 * S, y + math.sin(ang) * 1.6 * S
            t = i / max(1, steps)
            a = al * (1 - 0.75 * t)
            wi = max(0.6, wd * (1 - 0.6 * t))
            d.line([(x, y), (nx, ny)], fill=int(255 * a), width=max(1, int(round(wi * S))))
            x, y = nx, ny
            if depth < 3 and rng.random() < 0.045:
                grow(x, y, ang + rng.choice([-1, 1]) * rng.uniform(0.4, 0.9),
                     int(rng.integers(8, 22)), wd * 0.55, a * 0.8, depth + 1)
            if math.hypot(x - icx, y - icy) < 52 * S:
                break

    for _ in range(9):   # from the inner corner (left)
        grow(rng.uniform(12, 48) * S, (icy / S + rng.normal(0, 14)) * S, rng.normal(0, 0.3),
             int(rng.integers(35, 70)), rng.uniform(1.0, 1.9), rng.uniform(0.32, 0.55), 0)
    for _ in range(9):   # from the outer corner (right)
        grow(rng.uniform(232, 268) * S, (icy / S + rng.normal(0, 14)) * S, math.pi + rng.normal(0, 0.3),
             int(rng.integers(35, 70)), rng.uniform(1.0, 1.9), rng.uniform(0.30, 0.50), 0)
    for _ in range(5):   # from under the upper lid
        grow(rng.uniform(80, 200) * S, rng.uniform(18, 28) * S, math.pi / 2 + rng.normal(0, 0.3),
             int(rng.integers(14, 30)), rng.uniform(0.8, 1.4), rng.uniform(0.25, 0.40), 1)
    for _ in range(5):   # from above the lower lid
        grow(rng.uniform(80, 200) * S, rng.uniform(104, 112) * S, -math.pi / 2 + rng.normal(0, 0.3),
             int(rng.integers(14, 30)), rng.uniform(0.8, 1.4), rng.uniform(0.25, 0.40), 1)
    for _ in range(240):  # fine capillaries
        grow(rng.uniform(10, 270) * S, rng.uniform(16, 116) * S, rng.choice([0, math.pi]) + rng.normal(0, 0.6),
             int(rng.integers(4, 11)), 0.6, 0.20, 3)

    m = np.asarray(im.resize((w, h), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.45)), float) / 255
    a = (m * 0.75)[..., None]
    img = img * (1 - a) + np.array([176, 62, 64], float) * a
    return to_u8(img)

# ---------------------------------------------------------------- iris
def polar_sample(f, th, rho):
    nt, nr = f.shape
    t = (th / (2 * np.pi) % 1.0) * nt
    rr = np.clip(rho, 0, 1) * (nr - 1)
    t0 = np.floor(t).astype(int); ft = t - t0; t0 %= nt; t1 = (t0 + 1) % nt
    r0 = np.floor(rr).astype(int); fr = rr - r0; r1 = np.minimum(r0 + 1, nr - 1)
    return (f[t0, r0] * (1 - ft) * (1 - fr) + f[t1, r0] * ft * (1 - fr) +
            f[t0, r1] * (1 - ft) * fr + f[t1, r1] * ft * fr)

def bilinear(img, x, y):
    h, w = img.shape[:2]
    x = np.clip(x, 0, w - 1.001); y = np.clip(y, 0, h - 1.001)
    x0 = x.astype(int); y0 = y.astype(int); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) +
            img[y0 + 1, x0] * (1 - fx) * fy + img[y0 + 1, x0 + 1] * fx * fy)

def make_iris(rng, pal_name, photo=None, photo_pupil=0.33):
    P = IRIS_PAL[pal_name]
    S = 2
    N = IRIS_S * S
    yy, xx = np.mgrid[0:N, 0:N].astype(float)
    dx = (xx + 0.5) / S - IRIS_S / 2
    dy = (yy + 0.5) / S - IRIS_S / 2
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    rho = np.clip((r - IRIS_P0) / (IRIS_R - IRIS_P0), 0, 1)     # 0 = pupil edge, 1 = limbus
    lim_col = np.array(P['lim'], float)

    if photo is not None:                                       # ---- real photo path
        ph = Image.open(photo).convert('RGB')
        s = min(ph.size)
        ph = ph.crop(((ph.width - s) // 2, (ph.height - s) // 2, (ph.width + s) // 2, (ph.height + s) // 2))
        ph = np.asarray(ph, float)
        half = s / 2
        rp = (photo_pupil + rho * (1 - photo_pupil)) * half     # radial remap: pupil edge & limbus line up
        col = bilinear(ph, half + rp * np.cos(th), half + rp * np.sin(th))
        a = sstep(0.86, 1.0, rho)[..., None] * 0.35             # soften into a limbal ring
        col = col * (1 - a) + lim_col * a
    else:                                                       # ---- procedural path
        NT, NR = 240, 128
        f1 = noise((NT, NR), 0.9, 22, rng)                      # thin radial fibres
        f2 = noise((NT, NR), 2.6, 12, rng)                       # fibre bundles
        f3 = noise((NT, NR), 9.0, 7, rng)                       # slow colour patches
        rr = np.linspace(0, 1, NR)[None, :]
        tt = np.linspace(0, 2 * np.pi, NT, endpoint=False)[:, None]
        crypt = np.zeros((NT, NR))                              # dark crypts around the collarette
        for _ in range(16):
            t0 = rng.uniform(0, 2 * np.pi); r0 = rng.uniform(0.18, 0.5)
            dt = (tt - t0 + np.pi) % (2 * np.pi) - np.pi
            crypt += rng.uniform(0.5, 1.0) * np.exp(-((dt / rng.uniform(0.04, 0.09)) ** 2 + ((rr - r0) / rng.uniform(0.03, 0.06)) ** 2))
        furrow = np.exp(-((rr - 0.78) / 0.08) ** 2) * (0.5 + 0.5 * noise((NT, NR), 6, 0.01, rng)) * 0.18
        coll_r = 0.36 + 0.035 * noise((NT, 1), 5, 0.01, rng) + 0.015 * noise((NT, 1), 1.2, 0.01, rng)

        fib = polar_sample(f1, th, rho); fib2 = polar_sample(f2, th, rho); patch = polar_sample(f3, th, rho)
        t_fib = np.clip(0.5 + 0.34 * fib + 0.07 * fib2 + 0.05 * patch, 0, 1)[..., None]
        outer = lerp(np.array(P['o0'], float), np.array(P['o1'], float), t_fib)
        inner = lerp(np.array(P['i0'], float), np.array(P['i1'], float), t_fib)
        cr = polar_sample(coll_r * np.ones((1, NR)), th, rho)
        zone = sstep(-0.10, 0.06, rho - cr)[..., None]
        col = inner * (1 - zone) + outer * zone
        col *= (1 + 0.10 * np.exp(-((rho - 0.55) / 0.2) ** 2))[..., None]
        col *= (1 - 0.5 * np.clip(polar_sample(crypt, th, rho), 0, 1))[..., None]
        col *= (1 - polar_sample(furrow, th, rho))[..., None]
        a = (sstep(0.80, 1.0, rho) ** 1.3 * (0.78 + 0.22 * np.clip(0.5 + 0.5 * patch, 0, 1)))[..., None]
        col = col * (1 - a) + lim_col * a
        ruff = (1 - sstep(0.0, 0.10, rho))[..., None] * 0.65
        col = col * (1 - ruff) + np.array([38, 28, 22], float) * ruff

    col = np.where((r > IRIS_R)[..., None], lim_col, col)
    col = np.where((r < IRIS_P0)[..., None], np.array([6, 5, 5], float), col)
    out = Image.fromarray(to_u8(col)).resize((IRIS_S, IRIS_S), Image.BOX)
    return np.asarray(out)

# ---------------------------------------------------------------- corneal gloss
def make_gloss():
    S = 4
    N = IRIS_S * S
    c = IRIS_S / 2 * S
    im = Image.new('L', (N, N), 0)
    d = ImageDraw.Draw(im)
    cx, cy, w, h = c - 15 * S, c - 17 * S, 21 * S, 16 * S           # window-shaped soft box
    d.rounded_rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], radius=3 * S, fill=245)
    d.rectangle([cx - 0.9 * S, cy - h / 2, cx + 0.9 * S, cy + h / 2], fill=0)   # window frame
    d.rectangle([cx - w / 2, cy - 0.8 * S, cx + w / 2, cy + 0.8 * S], fill=0)
    a = np.asarray(im.filter(ImageFilter.GaussianBlur(0.8 * S)), float)
    sm = Image.new('L', (N, N), 0)                                  # faint second reflection
    ImageDraw.Draw(sm).ellipse([c + 12 * S, c + 12 * S, c + 20 * S, c + 17 * S], fill=85)
    a = np.maximum(a, np.asarray(sm.filter(ImageFilter.GaussianBlur(1.3 * S)), float))
    yy, xx = np.mgrid[0:N, 0:N].astype(float)                       # broad cornea sheen
    veil = 34 * np.clip(1 - np.hypot(xx - (c - 12 * S), yy - (c - 14 * S)) / (30 * S), 0, 1) ** 1.5
    a = np.clip(a + veil, 0, 255)
    return np.asarray(Image.fromarray(to_u8(a)).resize((IRIS_S, IRIS_S), Image.BOX))

# ---------------------------------------------------------------- lashes
def make_lashes(rng, upper=True):
    S = 4
    LH = LASH_UH if upper else LASH_LH
    im = Image.new('L', (W * S, LH * S), 0)
    d = ImageDraw.Draw(im)
    x = 7.0
    while x < W - 7:
        u = (x - CX) / HW
        if abs(u) < (0.97 if upper else 0.80):
            h = math.sqrt(max(0.0, 1 - u * u))
            if upper:
                L = min(LH - 1.5, (10 + 14 * h) * rng.uniform(0.78, 1.08))
                lean = u * 0.80 + rng.normal(0, 0.13)
                w0, a0 = rng.uniform(2.0, 2.8), 238
            else:
                L = (3.5 + 5.5 * h) * rng.uniform(0.6, 1.0)
                lean = u * 0.55 + rng.normal(0, 0.15)
                w0, a0 = rng.uniform(1.1, 1.5), 170
            pts = []
            n = 8
            for i in range(-1, n + 1):
                t = i / n
                pts.append(((x + L * 0.5 * lean * (t + t * t)) * S, L * t * S))
            for i in range(len(pts) - 1):
                t = max(0, i / n)
                wi = max(1, int(S * (w0 * (1 - t) + 0.6 * t)))
                d.line([pts[i], pts[i + 1]], fill=int(a0 - 95 * t), width=wi)
        x += rng.uniform(3.4, 4.8) if upper else rng.uniform(4.5, 6.5)
    return np.asarray(im.resize((W, LH), Image.BOX))

# ---------------------------------------------------------------- output
def rgb565(a):
    a = a.astype(np.uint16)
    return ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)

def emit(f, name, arr, kind):
    flat = arr.reshape(-1)
    fmt = '0x%04X' if kind == 16 else '%d'
    ctype = 'uint16_t' if kind == 16 else 'uint8_t'
    f.write('static const %s %s[%d] PROGMEM = {\n' % (ctype, name, flat.size))
    for i in range(0, flat.size, 16):
        f.write(','.join(fmt % int(v) for v in flat[i:i + 16]) + ',\n')
    f.write('};\n\n')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iris', default='blue', choices=IRIS_PAL.keys())
    ap.add_argument('--skin', default='light', choices=SKIN_TONES.keys())
    ap.add_argument('--iris-photo', help='square-ish macro photo of an iris (iris touching the crop edges)')
    ap.add_argument('--photo-pupil', type=float, default=0.33, help='pupil radius / iris radius in that photo')
    ap.add_argument('--use-png', metavar='DIR', help='load edited PNGs from DIR instead of generating')
    ap.add_argument('--seed', type=int, default=7)
    ap.add_argument('--out', default='eye_assets.h')
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)

    skin = make_skin(rng, a.skin)
    sclera = make_sclera(rng)
    iris = make_iris(rng, a.iris, a.iris_photo, a.photo_pupil)
    gloss = make_gloss()
    lash_up = make_lashes(rng, True)
    lash_dn = make_lashes(rng, False)

    if a.use_png:
        def ld(n, mode, old):
            p = os.path.join(a.use_png, n)
            if os.path.exists(p):
                print('  using', p)
                return np.asarray(Image.open(p).convert(mode))
            return old
        skin = ld('skin.png', 'RGB', skin); sclera = ld('sclera.png', 'RGB', sclera)
        iris = ld('iris.png', 'RGB', iris); gloss = ld('gloss.png', 'L', gloss)
        lash_up = ld('lash_up.png', 'L', lash_up); lash_dn = ld('lash_dn.png', 'L', lash_dn)

    os.makedirs('assets_png', exist_ok=True)
    for n, arr, mode in [('skin', skin, 'RGB'), ('sclera', sclera, 'RGB'), ('iris', iris, 'RGB'),
                         ('gloss', gloss, 'L'), ('lash_up', lash_up, 'L'), ('lash_dn', lash_dn, 'L')]:
        Image.fromarray(arr, mode).save('assets_png/%s.png' % n)

    assert skin.shape == (W, W, 3) and sclera.shape == (SCL_H, SCL_W, 3) and iris.shape == (IRIS_S, IRIS_S, 3)
    with open(a.out, 'w') as f:
        f.write('// AUTO-GENERATED by make_eye_assets.py - do not edit by hand\n#pragma once\n#include <stdint.h>\n\n')
        for k, v in [('EYE_W', W), ('EYE_CX', CX), ('EYE_CY', CY), ('EYE_HW', HW), ('EYE_AU', AU), ('EYE_AL', AL),
                     ('EYE_CLOSE_RISE', CLOSE_RISE), ('IRIS_S', IRIS_S), ('IRIS_R', IRIS_R), ('IRIS_P0', IRIS_P0),
                     ('SCL_W', SCL_W), ('SCL_H', SCL_H), ('SCL_X0', SCL_X0), ('SCL_Y0', SCL_Y0),
                     ('LASH_UH', LASH_UH), ('LASH_LH', LASH_LH)]:
            f.write('#define %s %d\n' % (k, v))
        f.write('#define EYE_SHAPE %.3ff\n\n' % SHAPE)
        emit(f, 'SKIN', rgb565(skin), 16)
        emit(f, 'SCLERA', rgb565(sclera), 16)
        emit(f, 'IRIS', rgb565(iris), 16)
        emit(f, 'GLOSS', gloss, 8)
        emit(f, 'LASH_U', lash_up, 8)
        emit(f, 'LASH_L', lash_dn, 8)
    kb = (skin.size + sclera.size + iris.size) * 2 // 3 / 1024 + (gloss.size + lash_up.size + lash_dn.size) / 1024
    print('wrote %s  (about %.0f KB of flash)' % (a.out, kb))

if __name__ == '__main__':
    main()
