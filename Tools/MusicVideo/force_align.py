"""Forced alignment of known lyrics to a clean vocal track (CTC, wav2vec2).

    force_align.py MODEL_DIR VOCAL.wav SECTIONS.json OUT.json

SECTIONS.json: [{"start": s, "end": s, "text": "words sung in that window"}, ...]
OUT.json:      [{"w": word, "t": start seconds, "e": end seconds, "score": 0..1}, ...]
MODEL_DIR is a wav2vec2 CTC model for the language (here
huggingface.co/jonatasgrosman/wav2vec2-large-xlsr-53-spanish). Unlike free transcription,
this knows the words in advance and only has to find where each one starts.
"""
import json, subprocess, sys
import numpy as np, torch
from transformers import Wav2Vec2ForCTC, Wav2Vec2Processor

mdir, vocal, sections, out = sys.argv[1:5]
proc = Wav2Vec2Processor.from_pretrained(mdir)
model = Wav2Vec2ForCTC.from_pretrained(mdir).eval()
vocab = proc.tokenizer.get_vocab()
blank, bar = vocab[proc.tokenizer.pad_token], vocab["|"]
pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", vocal, "-ac", "1", "-ar", "16000", "-f", "f32le", "-"],
                     check=True, capture_output=True).stdout
audio = np.frombuffer(pcm, np.float32)
STAR = -1  # wildcard at both ends: soaks up anything sung in the window that is not in the text


def viterbi(logp, toks):
    """Best monotonic path of toks (with optional blanks between) through the frames."""
    T = len(logp)
    star = logp[:, [i for i in range(logp.shape[1]) if i != blank]].max(1)
    seq = [blank]
    for t in toks:
        seq += [t, blank]
    S = len(seq)
    em = np.stack([star if s == STAR else logp[:, s] for s in seq], 1)  # T x S
    score = np.full((T, S), -1e30); back = np.zeros((T, S), np.int8)
    score[0, 0], score[0, 1] = em[0, 0], em[0, 1]
    skip = np.array([s >= 2 and seq[s] != blank and seq[s] != seq[s - 2] for s in range(S)])
    for t in range(1, T):
        stay = score[t - 1]
        one = np.concatenate([[-1e30], score[t - 1, :-1]])
        two = np.where(skip, np.concatenate([[-1e30, -1e30], score[t - 1, :-2]]), -1e30)
        best = np.maximum(np.maximum(stay, one), two)
        back[t] = np.where(best == stay, 0, np.where(best == one, 1, 2))
        score[t] = best + em[t]
    s = S - 1 if score[-1, S - 1] >= score[-1, S - 2] else S - 2
    path = [s]
    for t in range(T - 1, 0, -1):
        s -= int(back[t, s])
        path.append(s)
    return path[::-1], seq, em


res = []
for sec in json.load(open(sections, encoding="utf-8")):
    a, b = int(sec["start"] * 16000), int(sec["end"] * 16000)
    x = audio[a:b]
    with torch.no_grad():
        logits = model(torch.from_numpy(((x - x.mean()) / (x.std() + 1e-9)).copy())[None]).logits[0]
    logp = torch.log_softmax(logits, -1).numpy()
    dt = (len(x) / 16000) / len(logp)
    words = sec["text"].split()
    toks, owner = [STAR], [None]
    for k, w in enumerate(words):
        for ch in w.lower():
            if ch in vocab:
                toks.append(vocab[ch]); owner.append(k)
        toks.append(bar); owner.append(None)
    toks.append(STAR); owner.append(None)
    path, seq, em = viterbi(logp, toks)
    first, last, sc = {}, {}, {}
    for t, s in enumerate(path):
        if s % 2 == 1 and owner[s // 2] is not None:
            k = owner[s // 2]
            first.setdefault(k, t); last[k] = t
            sc.setdefault(k, []).append(float(np.exp(em[t, s])))
    for k, w in enumerate(words):
        res.append(dict(w=w, t=round(sec["start"] + first[k] * dt, 3), e=round(sec["start"] + (last[k] + 1) * dt, 3),
                        score=round(float(np.mean(sc[k])), 2)))
        print(f"{res[-1]['t']:7.2f} {res[-1]['e']:7.2f} {res[-1]['score']:.2f} {w}", flush=True)
json.dump(res, open(out, "w"), ensure_ascii=False, indent=0)
