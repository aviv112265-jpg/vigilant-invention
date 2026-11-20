import sys, json, subprocess
import numpy as np
from faster_whisper import WhisperModel
name, audio, out = sys.argv[1:4]
m = WhisperModel(name, device="cpu", compute_type="int8", cpu_threads=4)
pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', audio, '-ac', '1', '-ar', '16000', '-f', 'f32le', '-'],
                     check=True, capture_output=True).stdout
segs, info = m.transcribe(np.frombuffer(pcm, np.float32), language="he", word_timestamps=True, beam_size=5,
    condition_on_previous_text=False,
    initial_prompt="הריני מקשר עצמי באמירת העשרה מזמורים אלו לכל הצדיקים האמיתיים שבדורנו. לכו נרננה. מכתם לדוד שמרני אל כי חסיתי בך.")
res = []
for s in segs:
    print(f"{s.start:7.2f} {s.end:7.2f} {s.text}", flush=True)
    res.append({"start": s.start, "end": s.end, "text": s.text,
                "words": [{"s": w.start, "e": w.end, "w": w.word, "p": w.probability} for w in (s.words or [])]})
json.dump(res, open(out, "w"), ensure_ascii=False, indent=0)
