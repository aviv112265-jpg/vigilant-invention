#!/usr/bin/env python3
"""Beat-cut trailer-style music video from stock clips ("Toma Toma", Rio de Janeiro).

Every shot starts on the song's beat grid; cuts get faster through the drops. Each shot is
cropped to fill the frame, slowly pushed in, graded, letterboxed, then all are joined and the
titles and the song are laid on top.

    python3 Tools/MusicVideo/make_trailer.py SONG.wav CLIPS_DIR OUT.mp4 [--sheet qa.jpg]

CLIPS_DIR holds the source clips by name (see SHOTS): c01.webm ... for the Rio footage from
Wikimedia Commons, m<ID>.mp4 for Mixkit clips. Credits are in rio_trailer_credits.txt.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from multiprocessing import Pool

from PIL import Image, ImageDraw, ImageFont

W, H, FPS = 1920, 1080, 30
BAR = 138  # letterbox bars, 2.39:1
BEAT = 10.3333 / 16  # measured: a drop every 16 beats = 10.333 s (92.9 BPM)
T0 = 10.366 - 16 * BEAT  # time of beat 0
FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"

# (source, in-point seconds, beats, options). Options: speed, zoom (base crop zoom),
# ay (0 = keep the top of the frame, 1 = the bottom), slow (gentle shot: no cut punch).
R = dict(zoom=1.22, ay=0.25)  # Rio drive / hotel footage: crop away corner logos and dashboard
SHOTS = [
    # intro: 4 slow shots
    # (the hotel film cuts every ~1.2 s, so in-points sit just after its own cuts)
    ("c06", 17.6, 2, dict(R, slow=1)), ("c06", 19.45, 2, dict(R, slow=1)), ("c06", 32.2, 4, dict(R, slow=1)),
    ("c06", 26.04, 2, dict(R, slow=1, speed=0.9)), ("c06", 27.28, 2, dict(R, slow=1, speed=0.9)),
    ("m48358", 2.0, 4, dict(slow=1)),
    # drop A1: a cut every 2 beats
    ("c03", 151.0, 2, dict(R, speed=2)), ("m35540", 2.0, 2, {}), ("c01", 1.5, 2, dict(zoom=1.1)),
    ("m33343", 1.0, 2, {}), ("c04", 381.0, 2, dict(R, speed=2)), ("m1128", 4.0, 2, {}),
    ("c10", 3.0, 2, dict(zoom=1.05)), ("m20634", 3.0, 2, {}),
    # drop A2
    ("c06", 29.72, 2, dict(R, speed=0.82)), ("m39917", 2.0, 2, {}), ("c02", 24.0, 2, dict(zoom=1.1)), ("m43484", 0.2, 2, {}),
    ("m63", 3.0, 1, {}), ("c04", 455.0, 1, dict(R, speed=2)), ("m326", 2.0, 1, {}), ("m64", 5.0, 1, {}),
    # break: 5 slow shots, dusk into night
    ("m11270", 3.0, 4, dict(slow=1)), ("m44499", 1.0, 4, dict(slow=1)), ("c06", 154.34, 4, dict(R, slow=1, speed=0.76)),
    ("m42342", 2.0, 4, dict(slow=1)), ("m50998", 0.5, 4, dict(slow=1)),
    # drop B1: night, every 2 beats
    ("m42039", 3.0, 2, {}), ("m4344", 2.0, 2, {}), ("c06", 157.6, 2, R), ("m51295", 4.0, 2, {}),
    ("m6754", 5.0, 2, {}), ("m33906", 6.0, 2, {}), ("m4136", 0.5, 2, {}), ("m40105", 6.0, 2, {}),
    # drop B2: every beat
    ("c03", 211.0, 1, dict(R, speed=2)), ("m14105", 4.0, 1, {}), ("c04", 104.0, 1, dict(R, speed=2)),
    ("m341", 3.0, 1, {}), ("m36833", 5.0, 1, {}), ("m40369", 6.0, 1, {}), ("c01", 30.0, 1, dict(zoom=1.1)),
    ("m45453", 4.0, 1, {}), ("m27539", 10.0, 1, {}), ("m3621", 5.0, 1, {}), ("c04", 560.0, 1, dict(R, speed=2)),
    ("m52427", 2.0, 1, {}), ("c10", 9.0, 1, dict(zoom=1.05)), ("m43486", 8.0, 1, {}), ("m40080", 6.0, 1, {}),
    ("m831", 4.0, 1, {}),
    # drop B3
    ("m4151", 6.0, 1, {}), ("c03", 287.0, 1, dict(R, speed=2)), ("m44545", 3.0, 1, {}), ("m1070", 6.0, 1, {}),
    ("m48504", 4.0, 1, {}), ("c04", 2.0, 1, dict(R, speed=2)), ("m3625", 4.0, 1, {}), ("m35540", 8.0, 1, {}),
    # drop B4: half-beat flurry
    ("c06", 9.1, .5, R), ("m64", 9.0, .5, {}), ("c01", 14.0, .5, dict(zoom=1.1)), ("m4344", 7.0, .5, {}),
    ("c06", 34.2, .5, R), ("m33906", 12.0, .5, {}), ("c02", 28.0, .5, dict(zoom=1.1)), ("m4151", 9.0, .5, {}),
    # last hit, then black
    ("m48358", 7.0, 1.5, dict(slow=1)),
]
TAIL = 5.5  # seconds of black for the end title

GRADE = ("eq=saturation=1.32:contrast=1.08:gamma=0.96,"
         "colorbalance=rs=-0.05:bs=0.07:rm=0.03:rh=0.07:bh=-0.05,"
         "unsharp=5:5:0.6,vignette=PI/5,noise=alls=5:allf=t")


def frame_of(beat):
    return round((T0 + beat * BEAT) * FPS)


def find(clips, name):
    for ext in (".mp4", ".webm", ".ogv", ".mov"):
        p = os.path.join(clips, name + ext)
        if os.path.exists(p):
            return p
    raise SystemExit("missing clip: " + name)


def render_shot(job):
    k, src, tin, nframes, o, out = job
    speed, zoom, ay, slow = o.get("speed", 1), o.get("zoom", 1.0), o.get("ay", 0.5), o.get("slow", 0)
    dur = nframes / FPS
    bw, bh = int(W * zoom) // 2 * 2, int(H * zoom) // 2 * 2
    push = 0.05 if slow else 0.04
    punch = "" if slow else "+0.07*max(0,1-t/0.13)"
    z = f"(1+{push}*t/{dur:.3f}{punch})"
    vf = (f"setpts=(PTS-STARTPTS)/{speed},fps={FPS},"
          f"scale={bw}:{bh}:force_original_aspect_ratio=increase,crop={bw}:{bh},"
          f"crop={W}:{H}:(iw-{W})/2:(ih-{H})*{ay},"
          f"scale=eval=frame:w='2*trunc({W // 2}*{z})':h='2*trunc({H // 2}*{z})',crop={W}:{H},setsar=1,{GRADE}")
    if not slow:
        vf += ",eq=eval=frame:brightness='0.22*max(0,1-t/0.10)'"
    vf += f",drawbox=0:0:{W}:{BAR}:black:fill,drawbox=0:{H - BAR}:{W}:{BAR}:black:fill"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{tin:.3f}", "-t", f"{dur * speed + 1:.3f}", "-i", src,
                    "-an", "-vf", vf, "-frames:v", str(nframes), "-r", str(FPS),
                    "-c:v", "libx264", "-preset", "veryfast", "-crf", "15", "-pix_fmt", "yuv420p", out], check=True)
    got = int(subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v", "-show_entries",
                              "stream=nb_read_frames", "-of", "csv=p=0", out], capture_output=True, text=True).stdout or 0)
    return k, got == nframes, got


def title_png(path, big, small):
    """End title: original lettering, sunset gradient."""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    f1 = ImageFont.truetype(FONT, 190)
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    x = (W - sum(f1.getlength(c) + 14 for c in big)) / 2
    for c in big:
        d.text((x, 400), c, font=f1, fill=255)
        x += f1.getlength(c) + 14
    grad = Image.new("RGBA", (W, H))
    gd = ImageDraw.Draw(grad)
    for y in range(H):
        t = min(1, max(0, (y - 400) / 220))
        gd.line([(0, y), (W, y)], fill=(255, int(210 - 130 * t), int(90 + 110 * t), 255))
    im.paste(grad, (0, 0), mask)
    f2 = ImageFont.truetype(FONT, 40)
    d2 = ImageDraw.Draw(im)
    x = (W - sum(f2.getlength(c) + 16 for c in small)) / 2
    for c in small:
        d2.text((x, 650), c, font=f2, fill=(255, 255, 255, 230))
        x += f2.getlength(c) + 16
    im.save(path)


def caption_png(path, text):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    f = ImageFont.truetype(FONT, 44)
    d = ImageDraw.Draw(im)
    x = 150
    for c in text:
        d.text((x + 2, H - BAR - 92), c, font=f, fill=(0, 0, 0, 160))
        d.text((x, H - BAR - 94), c, font=f, fill=(255, 255, 255, 240))
        x += f.getlength(c) + 12
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song")
    ap.add_argument("clips")
    ap.add_argument("out")
    ap.add_argument("--sheet")
    ap.add_argument("--title", default="TOMA TOMA")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory(dir=os.path.dirname(os.path.abspath(a.out))) as tmp:
        jobs, beat = [], 0.0
        for k, (name, tin, beats, o) in enumerate(SHOTS):
            n = frame_of(beat + beats) - frame_of(beat)
            jobs.append((k, find(a.clips, name), tin, n, o, os.path.join(tmp, f"s{k:03d}.mp4")))
            beat += beats
        end = frame_of(beat)
        with Pool(os.cpu_count()) as pool:
            for k, ok, got in pool.imap_unordered(render_shot, jobs):
                if not ok:
                    raise SystemExit(f"shot {k} ({SHOTS[k][0]} @ {SHOTS[k][1]}) rendered {got} of {jobs[k][3]} frames")
        if a.sheet:
            lst = []
            for j in jobs:
                p = j[5][:-4] + ".jpg"
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", j[5], "-vf",
                                f"select=eq(n\\,{j[3] // 2}),scale=384:216,drawtext=text='{j[0]} {SHOTS[j[0]][0]}':"
                                "fontcolor=yellow:fontsize=20:x=4:y=4:box=1:boxcolor=black@0.6",
                                "-frames:v", "1", p], check=True)
                lst.append(Image.open(p))
            sheet = Image.new("RGB", (384 * 8, 216 * -(-len(lst) // 8)))
            for i, im in enumerate(lst):
                sheet.paste(im, (384 * (i % 8), 216 * (i // 8)))
            sheet.save(a.sheet, quality=85)
        cat = os.path.join(tmp, "list.txt")
        with open(cat, "w") as fh:
            fh.writelines(f"file '{j[5]}'\n" for j in jobs)
        title, cap = os.path.join(tmp, "title.png"), os.path.join(tmp, "cap.png")
        title_png(title, a.title, "RIO DE JANEIRO")
        caption_png(cap, "RIO DE JANEIRO, BRASIL")
        te = end / FPS
        total = te + TAIL
        fc = (f"[0:v]tpad=stop_mode=add:stop_duration={TAIL}:color=black,fade=t=in:st=0:d=1.2[v0];"
              f"[2:v]format=rgba,fade=t=in:st=1.5:d=0.6:alpha=1,fade=t=out:st=4.6:d=0.6:alpha=1[cap];"
              f"[3:v]format=rgba,fade=t=in:st={te + 0.25:.3f}:d=0.5:alpha=1,fade=t=out:st={total - 1.0:.3f}:d=0.9:alpha=1[ttl];"
              f"[v0][cap]overlay=enable='between(t,1.5,5.3)'[v1];[v1][ttl]overlay=enable='gte(t,{te + 0.25:.3f})'[v];"
              f"[1:a]atrim=0:{total:.3f},afade=t=out:st={te:.3f}:d={TAIL - 0.3:.3f}[a]")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", cat, "-i", a.song,
                        "-loop", "1", "-framerate", str(FPS), "-t", f"{total:.3f}", "-i", cap,
                        "-loop", "1", "-framerate", str(FPS), "-t", f"{total:.3f}", "-i", title,
                        "-filter_complex", fc, "-map", "[v]", "-map", "[a]", "-t", f"{total:.3f}",
                        "-c:v", "libx264", "-preset", "medium", "-crf", "19", "-maxrate", "10M", "-bufsize", "20M",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "256k", "-movflags", "+faststart", a.out], check=True)
    print(f"wrote {a.out}: {len(SHOTS)} shots, {total:.1f}s")


if __name__ == "__main__":
    sys.exit(main())
