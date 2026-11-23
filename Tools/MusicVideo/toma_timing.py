"""Build per-word timings for Toma Toma from the Whisper transcript + the lyric sheet."""
import json, sys, unicodedata, re, difflib
asr_path, out = sys.argv[1:3]
BEAT = 10.3333 / 16; T0 = 10.366 - 16 * BEAT
def norm(w):
    w = unicodedata.normalize("NFD", w.lower())
    return re.sub("[^a-z]", "", "".join(c for c in w if unicodedata.category(c) != "Mn"))
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
heard = [(w["w"].strip(), w["s"], w["e"]) for s in json.load(open(asr_path)) for w in s["words"]]
tomas = [h for h in heard if norm(h[0]) == "toma"]
rest = [h for h in heard if norm(h[0]) != "toma"]
# DP alignment of the non-"toma" lyric words to what was heard
n, m = len(LYR), len(rest)
sc = [[0.0] * (m + 1) for _ in range(n + 1)]; bt = [[0] * (m + 1) for _ in range(n + 1)]
sim = lambda a, b: 1.0 if a == b else difflib.SequenceMatcher(None, a, b).ratio()
for a in range(1, n + 1):
    for b in range(1, m + 1):
        r = sim(norm(LYR[a - 1]), norm(rest[b - 1][0]))
        best, how = sc[a - 1][b - 1] + (r * 2 if r >= 0.6 else -0.6), 0
        if sc[a - 1][b] - 0.25 > best: best, how = sc[a - 1][b] - 0.25, 1
        if sc[a][b - 1] - 0.25 > best: best, how = sc[a][b - 1] - 0.25, 2
        sc[a][b], bt[a][b] = best, how
t = [None] * n
a, b = n, m
while a and b:
    how = bt[a][b]
    if how == 0:
        if sim(norm(LYR[a - 1]), norm(rest[b - 1][0])) >= 0.6: t[a - 1] = rest[b - 1][1]
        a, b = a - 1, b - 1
    elif how == 1: a -= 1
    else: b -= 1
known = [k for k in range(n) if t[k] is not None]
for ka, kb in zip(known, known[1:]):  # short gaps: spread evenly
    gap = kb - ka - 1
    if gap and gap <= 3 and t[kb] - t[ka] < 3:
        for j in range(gap): t[ka + 1 + j] = t[ka] + (t[kb] - t[ka]) * (j + 1) / (gap + 1)
words = [dict(w=LYR[k], t=round(t[k], 2)) for k in range(n) if t[k] is not None]
missing = [(k, LYR[k]) for k in range(n) if t[k] is None]
# "toma": snap what was heard to the half-beat grid, drop pile-ups
snap = sorted({round(((x[1] - T0) / BEAT) * 2) / 2 for x in tomas})
# each chorus opens with the toma line twice; Whisper only catches the first, so echo it 8 beats later
for first in (60.0, 124.0, 174.0):
    line = [b for b in snap if first - 0.5 <= b < first + 6]
    if not any(first + 7.5 <= b < first + 14 for b in snap):
        snap += [b + 8 for b in line]
for b in sorted(set(snap)):
    words.append(dict(w="Toma", t=round(T0 + b * BEAT, 2), hook=1))
# lines Whisper missed, placed like the same line in the second chorus
have = {(x["w"], round(x["t"])) for x in words}
for w, tt in (("y", 58.0), ("no", 58.4), ("pares", 58.85), ("más", 59.43), ("y", 129.55)):
    if not any(abs(x["t"] - tt) < 0.5 and x["w"] == w for x in words):
        words.append(dict(w=w, t=tt))
words.sort(key=lambda x: x["t"])
json.dump(words, open(out, "w"), ensure_ascii=False, indent=0)
print(len(words), "words; missing:", missing)
for w in words: print(f"{w['t']:7.2f} {(w['t']-T0)/BEAT:6.1f} {w['w']}")
