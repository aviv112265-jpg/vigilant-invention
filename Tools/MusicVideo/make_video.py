#!/usr/bin/env python3
"""Audio-reactive music video renderer: "תיקון הכללי" (hard dubstep).

Dark mystic / kabbalistic look: Tree of Life carrying the ten psalms of the
Tikkun HaKlali, a ring of the 22 letters, a circular spectrum, embers, and the
title slamming on the kicks during the drops.

    python3 Tools/MusicVideo/make_video.py SONG.mp3 OUT.mp4
    python3 Tools/MusicVideo/make_video.py SONG.mp3 OUT.mp4 --start 60 --dur 20   # preview
    python3 Tools/MusicVideo/make_video.py SONG.mp3 --plot features.png          # analysis only

Needs: ffmpeg, numpy, Pillow (with raqm for right-to-left text).
"""
import argparse
import difflib
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from multiprocessing import Pool

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 1920, 1080
FPS = 30
SR = 22050
HOP = SR // FPS  # 735 samples -> exactly one analysis frame per video frame
NBARS = 48
CX, CY = W // 2, H // 2

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.path.join(HERE, "..", "Fonts", "NotoSansHebrew-Bold.ttf")
FONT_SERIF = "/usr/share/fonts/truetype/freefont/FreeSerifBold.ttf"
if not os.path.exists(FONT_SERIF):
    FONT_SERIF = FONT_BOLD

TITLE = "תיקון הכללי"
WORDS = [TITLE, "תיקון", "הכללי"]
LETTERS = "אבגדהוזחטיכלמנסעפצקרשת"
# The ten psalms of the Tikkun HaKlali, one per sefira.
PSALMS = ["ט״ז", "ל״ב", "מ״א", "מ״ב", "נ״ט", "ע״ז", "צ׳", "ק״ה", "קל״ז", "ק״נ"]

GOLD = np.array([255, 185, 70], float)
EMBER = np.array([255, 95, 25], float)
BLOOD = np.array([215, 22, 18], float)
HOT = np.array([255, 235, 200], float)

SEFIROT = [(0, 0), (1, 1), (-1, 1), (1, 3), (-1, 3), (0, 4), (1, 5), (-1, 5), (0, 6.3), (0, 8)]
PATHS = [(0, 1), (0, 2), (0, 5), (1, 2), (1, 3), (1, 5), (2, 4), (2, 5), (3, 4), (3, 5), (3, 6),
         (4, 5), (4, 7), (5, 6), (5, 7), (5, 8), (6, 7), (6, 8), (6, 9), (7, 8), (7, 9), (8, 9)]


def col(c, k=1.0):
    return tuple(int(max(0, min(255, v * k))) for v in c)


def mix(a, b, t):
    return a + (b - a) * t


def smooth(x, n):
    if n <= 1:
        return x
    k = np.hanning(n * 2 + 1)
    k /= k.sum()
    return np.convolve(np.pad(x, n, mode="edge"), k, mode="valid")


def norm(x, p=97):
    return np.clip(x / (np.percentile(x, p) + 1e-9), 0, 1.5)


# --------------------------------------------------------------------------- analysis
def analyze(path):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "s16le", "-"],
        check=True, capture_output=True).stdout
    x = np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0
    n = len(x) // HOP
    win = 2048
    xp = np.pad(x, (win // 2, win))
    hann = np.hanning(win).astype(np.float32)
    freqs = np.fft.rfftfreq(win, 1 / SR)
    edges = np.geomspace(30, 9000, NBARS + 1)
    mag = np.empty((n, win // 2 + 1), np.float32)
    for s in range(0, n, 2000):
        idx = np.arange(s, min(n, s + 2000))[:, None] * HOP + np.arange(win)[None, :]
        mag[s:s + 2000] = np.abs(np.fft.rfft(xp[idx] * hann, axis=1))

    def band(lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        if not m.any():
            m = np.arange(len(freqs)) == np.argmin(np.abs(freqs - (lo + hi) / 2))
        return np.sqrt((mag[:, m] ** 2).mean(axis=1))

    sub, bass, midb, high = band(25, 70), band(70, 250), band(250, 2500), band(2500, 9000)
    bars = np.stack([band(edges[i], edges[i + 1]) for i in range(NBARS)], axis=1)
    bars = np.log1p(bars * 20)
    bars /= np.percentile(bars, 99, axis=0, keepdims=True) + 1e-9
    bars = np.clip(bars, 0, 1.2)
    for i in range(1, n):  # fast attack, slow release
        bars[i] = np.maximum(bars[i], bars[i - 1] * 0.82)

    low = mag[:, (freqs >= 30) & (freqs < 160)]
    flux = np.maximum(np.diff(low, axis=0, prepend=low[:1]), 0).sum(axis=1)
    flux = norm(flux, 98)
    kicks = np.zeros(n, bool)
    last = -99
    thr = smooth(flux, FPS) * 1.3 + 0.12
    for i in range(1, n - 1):
        if flux[i] > thr[i] and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1] and i - last >= 5:
            kicks[i] = True
            last = i
    kick_env = np.zeros(n)
    e = 0.0
    for i in range(n):
        e = max(e * 0.80, flux[i] if kicks[i] else 0.0)
        kick_env[i] = min(e, 1.0)

    rms = np.sqrt((mag ** 2).mean(axis=1))
    lowe = np.sqrt(sub ** 2 + bass ** 2)
    inten = 0.6 * norm(smooth(lowe, FPS), 90) + 0.4 * norm(smooth(rms, FPS), 90)
    inten = np.clip(inten, 0, 1)
    raw_inten = inten
    inten = smooth(inten, int(FPS * 2.5))  # sections, not single bars
    lo_, hi_ = np.percentile(inten, 15), np.percentile(inten, 85)
    if hi_ - lo_ < 0.15:  # track with no dynamics: treat it all as high energy
        mode = np.clip(inten, 0, 1)
    else:
        mode = np.clip((inten - (lo_ + 0.45 * (hi_ - lo_))) / (0.25 * (hi_ - lo_)), 0, 1)
    mode = smooth(mode, FPS)

    drops = []
    for i in range(FPS * 2, n):
        if mode[i] > 0.75 and mode[i - FPS * 2:i - FPS // 2].mean() < 0.35 and \
                (not drops or i - drops[-1] > FPS * 8):
            j = i  # snap back to the kick that opened the drop
            for b in range(i, max(i - FPS, 0), -1):
                if kicks[b]:
                    j = b
            drops.append(j)
    flash = np.zeros(n)
    for d in drops:
        m = np.arange(n - d)
        flash[d:] = np.maximum(flash[d:], np.exp(-m / (FPS * 0.45)))

    f = {
        "n": n, "sub": norm(smooth(sub, 1)), "bass": norm(smooth(bass, 1)), "mid": norm(midb),
        "high": norm(smooth(high, 2)), "bars": bars, "kick": kick_env, "kicks": kicks,
        "nkick": np.cumsum(kicks), "inten": raw_inten, "mode": mode, "flash": flash, "drops": drops,
    }
    f["spin"] = np.cumsum(0.25 + 1.6 * mode + 1.2 * kick_env * mode) / FPS
    f["rise"] = np.cumsum(0.6 + 2.4 * mode) / FPS
    return f


def plot(f, path):
    n = f["n"]
    im = Image.new("RGB", (1600, 500), "black")
    d = ImageDraw.Draw(im)
    for name, c, row in (("inten", "white", 0), ("mode", "orange", 1), ("bass", "red", 2),
                         ("high", "cyan", 3), ("kick", "yellow", 4)):
        v = f[name]
        pts = [(x, row * 100 + 95 - 90 * min(1, v[min(n - 1, x * n // 1600)])) for x in range(1600)]
        d.line(pts, fill=c)
        d.text((4, row * 100 + 2), name, fill=c)
    for dr in f["drops"]:
        d.line([(dr * 1600 // n, 0), (dr * 1600 // n, 500)], fill="lime")
    im.save(path)


# --------------------------------------------------------------------------- lyrics
_MARKS = re.compile("[\u0591-\u05c7]")
_FINALS = str.maketrans("ךםןףץ", "כמנפצ")
_NAME = {"יהוה", "אדוני", "אדני", "יי", "ה"}


def plain(w):
    w = _MARKS.sub("", w)
    w = re.sub("[^\u05d0-\u05ea]", "", w)
    return w.translate(_FINALS)


def _sim(a, b):
    if a == b or (a in _NAME and b in _NAME):
        return 1.0
    return difflib.SequenceMatcher(None, a, b).ratio()


def lyric_lines(path, max_words=5):
    """Lyrics text -> display lines (lists of words), broken at verse ends and commas."""
    text = open(path, encoding="utf-8").read()
    lines = []
    for verse in re.split("[:.\n]+", text):
        for part in verse.split(","):
            ws = part.split()
            if not ws:
                continue
            k = math.ceil(len(ws) / max_words)
            per = math.ceil(len(ws) / k)
            lines += [ws[j:j + per] for j in range(0, len(ws), per)]
    return lines


def align_lyrics(lines, asr_path, total):
    """Give every sung line word times, from a Whisper word-level transcript (asr json)."""
    heard = [(plain(w["w"]), w["s"], w["e"]) for seg in json.load(open(asr_path, encoding="utf-8"))
             for w in seg["words"]]
    heard = [h for h in heard if h[0]]
    words = [(li, wi, plain(w)) for li, ws in enumerate(lines) for wi, w in enumerate(ws)]
    n, m = len(words), len(heard)
    sc = np.zeros((n + 1, m + 1), np.float32)
    bt = np.zeros((n + 1, m + 1), np.int8)
    for a in range(1, n + 1):
        wa = words[a - 1][2]
        for b in range(1, m + 1):
            r = _sim(wa, heard[b - 1][0])
            best, how = sc[a - 1, b - 1] + (r * 2 if r >= 0.6 else -0.6), 0
            if sc[a - 1, b] - 0.25 > best:
                best, how = sc[a - 1, b] - 0.25, 1  # lyric word nobody heard
            if sc[a, b - 1] - 0.25 > best:
                best, how = sc[a, b - 1] - 0.25, 2  # heard word not in the lyrics
            sc[a, b], bt[a, b] = best, how
    t0 = [None] * n
    t1 = [None] * n
    a, b = n, m
    while a and b:
        how = bt[a, b]
        if how == 0:
            if _sim(words[a - 1][2], heard[b - 1][0]) >= 0.6:
                t0[a - 1], t1[a - 1] = heard[b - 1][1], heard[b - 1][2]
            a, b = a - 1, b - 1
        elif how == 1:
            a -= 1
        else:
            b -= 1
    # fill short unheard runs between anchors; long runs were not sung, leave them out
    known = [k for k in range(n) if t0[k] is not None]
    for ka, kb in zip(known, known[1:]):
        gap = kb - ka - 1
        if gap and gap <= 6 and (t0[kb] - t1[ka]) / gap >= 0.12:
            for j in range(gap):
                t0[ka + 1 + j] = t1[ka] + (t0[kb] - t1[ka]) * j / gap
                t1[ka + 1 + j] = t1[ka] + (t0[kb] - t1[ka]) * (j + 1) / gap
    out = []
    k = 0
    for li, ws in enumerate(lines):
        ts = t0[k:k + len(ws)]
        te = t1[k:k + len(ws)]
        k += len(ws)
        got = [x for x in ts if x is not None]
        if len(got) * 2 < len(ws):
            continue
        for j in range(len(ws)):  # words still unknown inside a line: spread evenly
            if ts[j] is None:
                prev = max([x for x in te[:j] if x is not None], default=got[0] - 0.3)
                nxt = min([x for x in ts[j + 1:] if x is not None], default=prev + 0.4)
                ts[j], te[j] = prev, max(prev, nxt)
        ts = list(np.maximum.accumulate(ts))
        out.append({"words": ws, "t": ts, "end": max(x for x in te if x is not None)})
    out.sort(key=lambda L: L["t"][0])
    for L, nxt in zip(out, out[1:] + [None]):
        L["start"] = L["t"][0] - 0.12
        L["stop"] = min(L["end"] + 1.5, nxt["t"][0] - 0.12 if nxt else total)
        L["stop"] = max(L["stop"], L["start"] + 0.3)
    return out


# --------------------------------------------------------------------------- assets
class Assets:
    def __init__(self):
        self.words = [self._sprite(w, FONT_BOLD, 330) for w in WORDS]
        self.letters = [self._sprite(c, FONT_SERIF, 60) for c in LETTERS]
        self.big_letters = [self._sprite(c, FONT_SERIF, 150) for c in LETTERS]
        self.psalms = [self._sprite(p, FONT_BOLD, 34) for p in PSALMS]
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - CX) / (W * 0.62), (yy - CY) / (H * 0.62))
        self.vig = np.clip(1.12 - r ** 2.2, 0.0, 1.0)[..., None].astype(np.float32)
        rng = np.random.default_rng(7)
        self.em = rng.random((260, 6))  # embers: x, y, speed, sway freq, sway amp, size
        self._ly = {}
        self.fl = rng.random((46, 5))  # flying letters: x, y, speed, letter, size

    def lyric(self, idx, words):
        """One lyric line as separate words: (mask, dark halo, x offset from the line centre) each."""
        if idx not in self._ly:
            text = " ".join(words)
            size = 96
            kw = dict(direction="rtl", language="he")
            while ImageFont.truetype(FONT_BOLD, size).getlength(text, **kw) > W - 320:
                size -= 4
            fnt = ImageFont.truetype(FONT_BOLD, size)
            total = fnt.getlength(text, **kw)
            space = fnt.getlength(" ")
            out = []
            for k, w in enumerate(words):  # right to left
                right = total / 2 - fnt.getlength(" ".join(words[:k]), **kw) - (space if k else 0)
                im = self._sprite(w, FONT_BOLD, size)
                halo = im.filter(ImageFilter.GaussianBlur(16)).point(lambda v: min(255, v * 3))
                out.append((im, halo, right - fnt.getlength(w, **kw) / 2))
            # every word sits on the same baseline: sprites are cropped to their ink, so align by ascent
            asc = [fnt.getbbox(w, **kw) for w in words]
            mid = (min(b[1] for b in asc) + max(b[3] for b in asc)) / 2
            out = [(im, halo, dx, (b[1] + b[3]) / 2 - mid) for (im, halo, dx), b in zip(out, asc)]
            self._ly = {idx: out}  # keep only the current line
        return self._ly[idx]

    @staticmethod
    def _sprite(text, font, size):
        fnt = ImageFont.truetype(font, size)
        kw = dict(direction="rtl", language="he")
        box = ImageDraw.Draw(Image.new("L", (8, 8))).textbbox((0, 0), text, font=fnt, **kw)
        pad = size // 8
        im = Image.new("L", (box[2] - box[0] + pad * 2, box[3] - box[1] + pad * 2), 0)
        ImageDraw.Draw(im).text((pad - box[0], pad - box[1]), text, font=fnt, fill=255, **kw)
        return im


def stamp(img, sprite, cx, cy, color, scale=1.0):
    if scale != 1.0:
        sprite = sprite.resize((max(1, int(sprite.width * scale)), max(1, int(sprite.height * scale))),
                               Image.BILINEAR)
    img.paste(color, (int(cx - sprite.width / 2), int(cy - sprite.height / 2)), sprite)


def polygon(d, cx, cy, r, sides, rot, color, width, step=1):
    pts = [(cx + r * math.cos(rot + 2 * math.pi * k * step / sides),
            cy + r * math.sin(rot + 2 * math.pi * k * step / sides)) for k in range(sides + 1)]
    d.line(pts, fill=color, width=width, joint="curve")


# --------------------------------------------------------------------------- frame
def draw_fresh(i, f, A):
    """Everything drawn new this frame (before trails, bloom and glitch)."""
    t = i / FPS
    m = float(f["mode"][i])
    kick = float(f["kick"][i])
    bass = float(min(1, f["bass"][i]))
    sub = float(min(1, f["sub"][i]))
    high = float(min(1, f["high"][i]))
    spin = float(f["spin"][i])
    rise = float(f["rise"][i])
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    main = mix(GOLD, BLOOD, m * 0.75)
    hot = mix(GOLD, HOT, 0.3 + 0.7 * kick)

    # heart glow
    g = 0.05 + 0.08 * bass + 0.06 * kick * m
    for k in range(6):
        rr = 520 - k * 80 + 40 * sub
        d.ellipse([CX - rr, CY - rr, CX + rr, CY + rr], fill=col(mix(BLOOD, EMBER, k / 6), g * (0.25 + k * 0.17)))

    # embers
    em = A.em
    ex = (em[:, 0] * W + np.sin(t * (0.3 + em[:, 3]) + em[:, 3] * 40) * (20 + 60 * em[:, 4])) % W
    ey = (em[:, 1] * H - rise * (40 + 140 * em[:, 2])) % H
    fl = 0.45 + 0.55 * np.sin(t * (2 + 5 * em[:, 4]) + em[:, 0] * 50)
    for k in range(len(em)):
        s = 1 + 2.5 * em[k, 5] * (0.6 + 0.6 * m)
        d.ellipse([ex[k] - s, ey[k] - s, ex[k] + s, ey[k] + s],
                  fill=col(mix(EMBER, GOLD, em[k, 5]), (0.25 + 0.6 * fl[k]) * (0.5 + 0.5 * m + 0.3 * high)))

    # letters flying up, mostly at the sides
    for k, p in enumerate(A.fl):
        side = -1 if k % 2 else 1
        fx = CX + side * (560 + p[0] * 380) + math.sin(t * 0.4 + k) * 25
        fy = (p[1] * (H + 300) - rise * (25 + 60 * p[2])) % (H + 300) - 150
        a = (0.10 + 0.22 * p[4]) * (0.7 + 0.6 * math.sin(t * 0.8 + k * 1.7) ** 2) * (1 + 0.8 * kick * m)
        stamp(img, A.big_letters[int(p[3] * 22) % 22], fx, fy, col(main, a), 0.45 + 0.75 * p[4])

    # circular spectrum
    R0 = 380 + 16 * kick + 10 * sub
    bars = f["bars"][i]
    for k in range(NBARS * 2):
        v = float(bars[k if k < NBARS else 2 * NBARS - 1 - k])
        ang = -math.pi / 2 + math.pi * (k + 0.5) / NBARS + spin * 0.15
        ln = 6 + v * (70 + 90 * m)
        c, s = math.cos(ang), math.sin(ang)
        d.line([(CX + c * R0, CY + s * R0), (CX + c * (R0 + ln), CY + s * (R0 + ln))],
               fill=col(mix(main, hot, v * 0.6), 0.35 + 0.65 * v), width=5)

    # rings + the 22 letters
    for rr, wd, k in ((R0 - 8, 2, 0.5), (R0 - 62, 1, 0.3), (300 + 10 * kick, 1, 0.22)):
        d.ellipse([CX - rr, CY - rr, CX + rr, CY + rr], outline=col(main, k + 0.3 * kick), width=wd)
    lit = int(f["nkick"][i]) % 22
    for k in range(22):
        ang = -spin * 0.35 + 2 * math.pi * k / 22
        on = 1.0 if k == lit else 0.0
        stamp(img, A.letters[k], CX + math.cos(ang) * (R0 - 35), CY + math.sin(ang) * (R0 - 35),
              col(mix(main, HOT, on * kick), 0.45 + 0.55 * on * kick + 0.2 * m), 0.72 + 0.25 * on * kick)

    # hexagram + nested polygons (drops)
    if m > 0.02:
        r = 290 + 46 * kick + 14 * sub
        a = m * (0.5 + 0.5 * kick)
        for off in (0, math.pi):
            polygon(d, CX, CY, r, 3, spin * 0.5 + off - math.pi / 2, col(hot, a), 4)
        polygon(d, CX, CY, r * 0.62, 6, -spin * 0.8, col(main, a * 0.8), 2)
        polygon(d, CX, CY, r * 1.12, 10, spin * 0.25, col(main, a * 0.45), 2, step=3)

    # Tree of Life with the ten psalms (breakdowns; ghosted in drops)
    sing = float(f["sing"][i])
    ta = ((1 - m) * 0.95 + 0.10) * (1 - 0.55 * sing)
    sc = 94 * (1 + 0.05 * kick * m + 0.012 * math.sin(t * 0.7))
    pts = [(CX + x * sc * 1.55, CY + (y - 4.05) * sc * 0.93) for x, y in SEFIROT]
    stage = int(t / 1.7) % 12  # psalms light one by one, then all ten glow
    for a_, b_ in PATHS:
        live = 0.55 if (a_ < stage and b_ < stage) else 0.25
        d.line([pts[a_], pts[b_]], fill=col(GOLD, ta * (live + 0.25 * bass)), width=2)
    for k, (px, py) in enumerate(pts):
        on = k < stage
        now = k == stage - 1
        rr = 34 + (7 * kick if now else 0)
        d.ellipse([px - rr, py - rr, px + rr, py + rr], fill=col(BLOOD, ta * (0.30 if on else 0.08)),
                  outline=col(mix(GOLD, HOT, 0.7 if now else 0), ta * (1.0 if on else 0.4)), width=3 if on else 2)
        if on and ta > 0.2:
            stamp(img, A.psalms[k], px, py, col(mix(GOLD, HOT, 0.8 if now else 0.2), ta))

    # title
    intro = min(1.0, t / 4.0)
    if m < 0.98 or sing > 0.02:  # quiet title at the bottom (calm parts, and whenever lyrics show)
        a = max((1 - m) ** 2, sing) * intro * (0.7 + 0.3 * bass)
        stamp(img, A.words[0], CX, H - 70, col(mix(GOLD, HOT, 0.35), a), 0.24)
    m *= 1 - sing  # the big title steps aside for the lyrics
    if m > 0.02:  # drop: the words slam on the kicks
        w = A.words[int(f["nkick"][i]) // 2 % 3 if m > 0.6 else 0]
        scale = (0.72 if w is A.words[0] else 1.05) * (0.86 + 0.20 * kick + 0.05 * sub)
        w = w.resize((int(w.width * scale), int(w.height * scale)), Image.BILINEAR)
        tcol = mix(BLOOD, HOT, 0.35 + 0.65 * kick)
        stamp(img, w, CX, CY - 14, col(tcol, m * 0.30))  # dim copy feeds the trails
        title = (w, col(tcol, 1.0), m)
    else:
        title = None
    return img, title


def render_frames(i0, i1, f, A, sink):
    warm = max(0, i0 - 45)
    prev = Image.new("RGB", (W, H), (0, 0, 0))
    for i in range(warm, i1):
        t = i / FPS
        m = float(f["mode"][i])
        kick = float(f["kick"][i])
        sub = float(min(1, f["sub"][i]))
        high = float(min(1, f["high"][i]))
        rng = np.random.default_rng(i)

        fresh, title = draw_fresh(i, f, A)
        # trails: last frame zooms outward and fades
        z = 1 / (1.012 + 0.030 * m + 0.030 * kick * m)
        ang = math.radians(0.25 + 0.5 * m) * (1 if int(t / 8) % 2 else -1)
        c, s = math.cos(ang) * z, math.sin(ang) * z
        trail = prev.transform((W, H), Image.AFFINE,
                               (c, -s, CX - c * CX + s * CY, s, c, CY - s * CX - c * CY), Image.BILINEAR)
        decay = 0.80 + 0.04 * m
        trail = trail.point([int(v * decay) for v in range(256)] * 3)
        prev = ImageChops.screen(trail, fresh)
        if i < i0:
            continue
        small = prev.resize((W // 4, H // 4), Image.BILINEAR).filter(ImageFilter.GaussianBlur(7))
        bk = 0.45 + 0.25 * kick
        bloom = small.resize((W, H), Image.BILINEAR).point([int(v * bk) for v in range(256)] * 3)
        frame = ImageChops.screen(prev, bloom)
        if title:  # crisp title on top: dark halo, then the letters
            w, c, ta = title
            halo = w.filter(ImageFilter.GaussianBlur(14)).point(lambda v: min(255, int(v * 2.2 * ta)))
            stamp(frame, halo, CX, CY - 14, (0, 0, 0))
            stamp(frame, w.point(lambda v: int(v * ta)), CX, CY - 14, c)

        L = f["lyric"][i]
        if L is not None:  # sung line in the centre: each word pops in on its own time
            out_a = min(1.0, (L["stop"] - t) / 0.25)
            pulse = 1 + 0.07 * kick * m  # the line punches with the kick in the drops
            for (im, halo, dx, dy), tw in zip(A.lyric(id(L), L["words"]), L["t"]):
                p = (t - tw + 0.04) / 0.24
                if p <= 0 or out_a <= 0:
                    continue
                q = min(1.0, p) - 1
                pop = (0.55 + 0.45 * (1 + 2.6 * q ** 3 + 1.6 * q ** 2)) * pulse  # soft overshoot, then settle
                fade = [int(v * min(1.0, p * 2.5) * out_a) for v in range(256)]
                heat = max(0.0, 1 - (t - tw) / 0.7)  # arrives white-hot, cools to gold
                x, y = CX + dx * pulse, CY + dy * pulse
                stamp(frame, halo.point(fade), x, y, (0, 0, 0), pop)
                stamp(frame, im.point(fade), x, y, col(mix(GOLD, HOT, 0.25 + 0.75 * max(heat, 0.5 * kick))), pop)
        # post: shake, chromatic split, slice glitch, drop flash, vignette, fades
        a = np.asarray(frame)
        sh = int(m * (4 + 14 * sub) * kick)
        if sh:
            a = np.roll(a, (int(rng.integers(-sh, sh + 1)), int(rng.integers(-sh, sh + 1))), axis=(0, 1))
        k = int(2 + m * (2 + 9 * kick) + 2 * high * m)
        a = np.stack([np.roll(a[:, :, 0], k, axis=1), a[:, :, 1], np.roll(a[:, :, 2], -k, axis=1)], axis=2)
        if m > 0.4 and (kick > 0.55 or rng.random() < 0.10 * high):
            for _ in range(int(rng.integers(2, 7))):
                y0 = int(rng.integers(0, H - 10))
                hh = int(rng.integers(6, 90))
                a[y0:y0 + hh] = np.roll(a[y0:y0 + hh], int(rng.integers(-160, 161)), axis=1)
        out = a.astype(np.float32)
        fl = float(f["flash"][i])
        if fl > 0.01:
            out += fl * 150 * np.array([1.0, 0.85, 0.6], np.float32)
        fade = min(1.0, t / 2.0, (f["n"] - 1 - i) / (FPS * 4.0))
        out *= A.vig * max(0.0, fade)
        if i % 2:  # light scanlines
            out[::3] *= 0.93
        sink.write(np.clip(out, 0, 255).astype(np.uint8).tobytes())


def encoder(path, crf):
    return subprocess.Popen(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
         "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", str(crf),
         "-maxrate", "6M", "-bufsize", "12M",
         "-pix_fmt", "yuv420p", path], stdin=subprocess.PIPE)


_F = None


def _job(args):
    i0, i1, path, crf = args
    enc = encoder(path, crf)
    render_frames(i0, i1, _F, Assets(), enc.stdin)
    enc.stdin.close()
    if enc.wait():
        raise RuntimeError("ffmpeg failed on " + path)
    return path


def main():
    global _F
    ap = argparse.ArgumentParser()
    ap.add_argument("audio")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--start", type=float, default=0)
    ap.add_argument("--dur", type=float, default=0)
    ap.add_argument("--crf", type=int, default=22)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--plot")
    ap.add_argument("--lyrics", help="lyrics text file")
    ap.add_argument("--asr", help="word-level transcript json (see transcribe.py), needed with --lyrics")
    ap.add_argument("--still", nargs=2, metavar=("SECONDS", "PNG"))
    a = ap.parse_args()

    f = _F = analyze(a.audio)
    print(f"{f['n'] / FPS:.1f}s, {int(f['kicks'].sum())} kicks, drops at",
          [f"{d // FPS // 60}:{d // FPS % 60:02d}" for d in f["drops"]], flush=True)
    f["lyric"] = [None] * f["n"]
    sing = np.zeros(f["n"])
    if a.lyrics:
        timed = align_lyrics(lyric_lines(a.lyrics), a.asr, f["n"] / FPS)
        for L in timed:
            s0, s1 = int(L["start"] * FPS), min(f["n"], int(L["stop"] * FPS))
            f["lyric"][s0:s1] = [L] * (s1 - s0)
            sing[s0:s1] = 1
            print(f"{L['start']:7.2f} {L['stop']:7.2f}  {' '.join(L['words'])}")
        print(len(timed), "lyric lines timed", flush=True)
    f["sing"] = np.clip(smooth(sing, FPS) * 1.0, 0, 1)
    if a.plot:
        plot(f, a.plot)
    if a.still:
        i = int(float(a.still[0]) * FPS)

        class Buf:
            def write(self, b):
                Image.frombytes("RGB", (W, H), b).save(a.still[1])
        render_frames(i, i + 1, f, Assets(), Buf())
    if not a.out:
        return
    i0 = int(a.start * FPS)
    i1 = min(f["n"], i0 + int(a.dur * FPS)) if a.dur else f["n"]
    with tempfile.TemporaryDirectory(dir=os.path.dirname(os.path.abspath(a.out))) as tmp:
        step = max(FPS * 5, math.ceil((i1 - i0) / (a.jobs * 4)))
        jobs = [(s, min(i1, s + step), os.path.join(tmp, f"{k:04d}.mp4"), a.crf)
                for k, s in enumerate(range(i0, i1, step))]
        with Pool(a.jobs) as pool:
            parts = []
            for p in pool.imap(_job, jobs):
                parts.append(p)
                print(f"  {len(parts)}/{len(jobs)} chunks", flush=True)
        lst = os.path.join(tmp, "list.txt")
        with open(lst, "w") as fh:
            fh.writelines(f"file '{p}'\n" for p in parts)
        subprocess.run(
            ["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst,
             "-ss", f"{i0 / FPS:.3f}", "-t", f"{(i1 - i0) / FPS:.3f}", "-i", a.audio,
             "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac", "-b:a", "320k",
             "-movflags", "+faststart", "-shortest", a.out], check=True)
    print("wrote", a.out)


if __name__ == "__main__":
    sys.exit(main())
