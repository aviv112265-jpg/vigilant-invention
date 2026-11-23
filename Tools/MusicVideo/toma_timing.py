"""Per-word lyric timings for Toma Toma.

    toma_timing.py ASR_VOCALS.json ASR_MIX.json VOCALS.wav OUT.json

Each word gets the moment it STARTS: Whisper's word start on the separated vocal stem
(see vocals.py), nudged onto the nearest rise in the voice. Words the stem transcript lost
fall back to the full-mix transcript. Whisper cannot count the fast "toma" hook, so those come
straight from the voice: in each stretch where it is sung (HOOKS below) every vocal attack is
one TOMA, and once the drop lands they run on the half-beat grid that best fits the attacks.
"""
import difflib, json, re, sys, unicodedata, wave
import numpy as np

asr_v, asr_m, vocals, out = sys.argv[1:5]
LYR = """Mírame bien baby / yo sé lo que quieres / paso lento suave / tú no te detienes /
Trae la cintura / suéltate en el suelo / yo te digo al oído / y tú rompes el hielo /
Sube la presión / no mires atrás / cuando cae el beat / tú lo pides más /
Dale sin freno / pégate más / y no pares más /
Tu cara dice todo / aunque no hables nada / la noche te busca / tú ya estás marcada /
Brinca conmigo / sin explicación / si tú me lo das / yo doy la razón /
Sube la presión / no mires atrás / cuando cae el beat / tú lo pides más /
Dale sin freno / pégate más / y no pares más /
Se acerca se va / vuelve a caer / tu nombre en mi boca / me hace correr /
Dale sin freno / pégate más / y no pares más""".replace("/", " ").split()
# stretches where the hook is sung: from its first "toma" to just before the next lyric line
# (start s, end s, beat where the drop lands inside it, or None)
HOOKS = [(7.90, 19.45, 16), (38.84, 49.65, 64), (54.60, 57.55, None), (80.14, 90.85, 128), (95.62, 98.90, None),
         (112.40, 120.95, 176), (126.56, 129.40, None)]
BEAT = 10.3333 / 16
T0 = 10.366 - 16 * BEAT


def norm(w):
    w = unicodedata.normalize("NFD", w.lower())
    return re.sub("[^a-z]", "", "".join(c for c in w if unicodedata.category(c) != "Mn"))


def align(path):
    """Start time of each lyric word (None where it was not heard), by sequence alignment."""
    heard = [(norm(w["w"]), w["s"]) for s in json.load(open(path)) for w in s["words"]]
    heard = [h for h in heard if h[0] and not h[0].startswith("toma") and h[0] != "gracias"
             and not any(a <= h[1] < b for a, b, _ in HOOKS)]
    n, m = len(LYR), len(heard)
    sim = lambda a, b: 1.0 if a == b else difflib.SequenceMatcher(None, a, b).ratio()
    sc = np.zeros((n + 1, m + 1)); bt = np.zeros((n + 1, m + 1), int)
    for a in range(1, n + 1):
        for b in range(1, m + 1):
            r = sim(norm(LYR[a - 1]), heard[b - 1][0])
            best, how = sc[a - 1, b - 1] + (r * 2 if r >= 0.6 else -0.6), 0
            if sc[a - 1, b] - 0.25 > best: best, how = sc[a - 1, b] - 0.25, 1
            if sc[a, b - 1] - 0.25 > best: best, how = sc[a, b - 1] - 0.25, 2
            sc[a, b], bt[a, b] = best, how
    t = [None] * n
    a, b = n, m
    while a and b:
        how = bt[a, b]
        if how == 0:
            if sim(norm(LYR[a - 1]), heard[b - 1][0]) >= 0.6: t[a - 1] = heard[b - 1][1]
            a, b = a - 1, b - 1
        elif how == 1: a -= 1
        else: b -= 1
    return t


tv, tm = align(asr_v), align(asr_m)
t = [a if a is not None else b for a, b in zip(tv, tm)]
src = ["stem" if a is not None else ("mix" if b is not None else "-") for a, b in zip(tv, tm)]
known = [k for k in range(len(t)) if t[k] is not None]
for ka, kb in zip(known, known[1:]):  # a word or two nobody heard, between two that were: spread evenly
    gap = kb - ka - 1
    if gap and gap <= 3 and t[kb] - t[ka] < 3:
        for j in range(gap):
            t[ka + 1 + j] = t[ka] + (t[kb] - t[ka]) * (j + 1) / (gap + 1); src[ka + 1 + j] = "fill"

# nudge each start onto the steepest rise of the voice close by
w = wave.open(vocals); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, w.getnchannels()).mean(1) / 32768
hop = sr // 200  # 5 ms
env = np.sqrt((x[:len(x) // hop * hop].reshape(-1, hop) ** 2).mean(1))
env = np.convolve(env, np.hanning(9) / np.hanning(9).sum(), mode="same")
rise = np.maximum(env[4:] - env[:-4], 0); peak = np.percentile(env, 95)
order = [k for k in range(len(t)) if t[k] is not None]
for i, k in enumerate(order):
    lo = t[k] - 0.08 if i == 0 else max(t[k] - 0.08, (t[order[i - 1]] + t[k]) / 2)
    hi = t[k] + 0.14 if i == len(order) - 1 else min(t[k] + 0.14, (t[order[i + 1]] + t[k]) / 2)
    a, b = int(lo * 200), int(hi * 200)
    if b > a and rise[a:b].max() > 0.12 * peak:
        t[k] = (a + int(rise[a:b].argmax())) / 200
for k in range(len(t) - 1):  # a lone unheard "y": just ahead of the word it leads into
    if t[k] is None and t[k + 1] is not None and LYR[k] == "y":
        t[k] = t[k + 1] - 0.4; src[k] = "fill"
words = [dict(w=LYR[k], t=round(t[k], 3)) for k in range(len(t)) if t[k] is not None]

# the hook, from the voice itself
r6 = np.maximum(env[6:] - env[:-6], 0) / peak
def attacks(a, b):
    got = []
    for i in range(int(a * 200), min(int(b * 200), len(r6) - 13)):
        if r6[i] > 0.18 and r6[i] == r6[max(0, i - 12):i + 13].max() and (not got or i / 200 - got[-1] > 0.12):
            got.append(i / 200)
    return [(x - T0) / BEAT for x in got]  # in beats
for a, b, drop in HOOKS:
    ba, bb = (a - T0) / BEAT, (b - T0) / BEAT
    on = attacks(a, b)
    split = drop if drop else bb
    hook = [ba]  # sung phrase: one TOMA per attack, at least 0.4 beat apart
    for x in on:
        if x < split - 0.3 and x - hook[-1] >= 0.4:
            hook.append(x)
    if drop:  # over the drop: every half beat, at the phase the attacks sit on
        late = [x for x in on if x >= drop]
        phase = max(np.arange(0, 0.5, 0.01), key=lambda ph: sum(abs(((x - ph) * 2 + 0.5) % 1 - 0.5) < 0.12 for x in late))
        x = drop + phase if phase >= 0.2 else drop + phase + 0.5
        last = min(bb - 0.3, max(late) + 0.3) if late else drop
        while x <= last:
            hook.append(x); x += 0.5
    print(f"hook {a:6.2f}-{b:6.2f}: {len(hook)} toma", ("(%d before the drop, %d after)" % (sum(h < drop for h in hook), sum(h >= drop for h in hook))) if drop else "")
    words += [dict(w="Toma", t=round(T0 + h * BEAT, 3), hook=1) for h in hook]
words.sort(key=lambda x: x["t"])
json.dump(words, open(out, "w"), ensure_ascii=False, indent=0)
print(len(words), "entries; not found:", [(k, LYR[k]) for k in range(len(t)) if t[k] is None])
for k in range(len(t)):
    if t[k] is not None: print(f"{t[k]:7.2f} {src[k]:5s} {LYR[k]}")
