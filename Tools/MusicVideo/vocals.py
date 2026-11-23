"""Pull the vocal stem out of a song with HTDemucs: vocals.py MODEL_DIR SONG OUT.wav

MODEL_DIR holds 955717e8.json and 955717e8.safetensors from huggingface.co/adefossez/HTDemucs.
Lyrics time far better against the bare voice than against the full mix.
"""
import json, subprocess, sys, wave
from fractions import Fraction
import numpy as np, torch
from safetensors.torch import load_file
from demucs.htdemucs import HTDemucs
from demucs.apply import apply_model

mdir, song, out = sys.argv[1:4]
meta = json.load(open(f"{mdir}/955717e8.json"))
kw = json.loads(meta["kwargs"]) if isinstance(meta["kwargs"], str) else meta["kwargs"]
for k, v in list(kw.items()):
    if isinstance(v, dict) and v.get("_type") == "fraction":
        kw[k] = Fraction(v["numerator"], v["denominator"])
model = HTDemucs(**kw)
model.load_state_dict(load_file(f"{mdir}/955717e8.safetensors"))
model.eval()
pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", song, "-ac", "2", "-ar", "44100", "-f", "f32le", "-"],
                     check=True, capture_output=True).stdout
x = torch.from_numpy(np.frombuffer(pcm, np.float32).reshape(-1, 2).T.copy())
ref = x.mean(0)
x = (x - ref.mean()) / ref.std()
with torch.no_grad():
    y = apply_model(model, x[None], split=True, overlap=0.25, progress=False)[0]
v = y[model.sources.index("vocals")] * ref.std() + ref.mean()
v = (v.clamp(-1, 1).T.numpy() * 32767).astype(np.int16)
with wave.open(out, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(v.tobytes())
print("wrote", out)
