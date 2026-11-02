# MISHPACHA - synthesizes the game's placeholder sounds and imports them as Sound Waves.
#
# Everything is generated from math (noise, oscillators, envelopes): no samples, no licenses.
# Run (editor closed):
#   UnrealEditor-Cmd.exe "<project>\Mishpacha.uproject" -run=pythonscript -script="<project>\Tools\CreateSounds.py" -unattended -nosplash
# Output: /Game/Mishpacha/Audio/S_*  (the C++ code loads them by name and stays silent without them).

import math
import os
import random
import struct
import wave

import unreal

RATE = 44100
FOLDER = "/Game/Mishpacha/Audio"
OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "GeneratedAudio")
os.makedirs(OUT_DIR, exist_ok=True)
rng = random.Random(1909)


def write_wav(name, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.89 / peak
    path = os.path.join(OUT_DIR, name + ".wav")
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples))
    return path


def lowpass(samples, cutoff):
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    out, y = [], 0.0
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out


def crossfade_loop(samples, fade_seconds=0.05):
    """Blend the tail into the head so the loop has no click."""
    n = int(fade_seconds * RATE)
    body = samples[:-n]
    for i in range(n):
        t = i / n
        body[i] = body[i] * t + samples[len(samples) - n + i] * (1.0 - t)
    return body


# ----- Gunshots: a noise crack with a fast decay, a low thump, optional echo -----
def gunshot(seconds, crack_decay, body_decay, body_hz, cutoff, echo=0.0):
    n = int(seconds * RATE)
    noise = [rng.uniform(-1.0, 1.0) for _ in range(n)]
    noise = lowpass(noise, cutoff)
    out = []
    for i in range(n):
        t = i / RATE
        crack = noise[i] * math.exp(-t * crack_decay)
        thump = math.sin(2 * math.pi * body_hz * t * (1.0 - 0.3 * t)) * math.exp(-t * body_decay) * 0.8
        click = 1.5 if i < 40 else 0.0
        out.append(crack + thump + click * rng.uniform(-1, 1))
    if echo > 0.0:
        delay = int(0.13 * RATE)
        for i in range(delay, n):
            out[i] += out[i - delay] * echo
    return out


# ----- Engine: integer-Hz harmonics over exactly one second, so the loop is seamless -----
def engine_loop():
    n = RATE
    out = []
    for i in range(n):
        t = i / RATE
        s = 0.0
        for harmonic, amp in ((55, 1.0), (110, 0.6), (165, 0.35), (220, 0.25), (330, 0.12)):
            s += amp * math.sin(2 * math.pi * harmonic * t)
        s *= 0.8 + 0.2 * math.sin(2 * math.pi * 27 * t)  # cylinder pulses
        out.append(s + rng.uniform(-0.08, 0.08))
    return lowpass(out, 1400)


# ----- Siren: a 4-second wail, up and down -----
def siren_loop():
    seconds = 4.0
    n = int(seconds * RATE) + int(0.05 * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        freq = 650.0 + 600.0 * (0.5 - 0.5 * math.cos(2 * math.pi * t / seconds))
        phase += 2 * math.pi * freq / RATE
        s = math.sin(phase) + 0.35 * math.sin(2 * phase) + 0.15 * math.sin(3 * phase)
        out.append(s)
    return crossfade_loop(out)


# ----- Radio: two original 8-bar loops -----
def note_hz(semitones_from_a4):
    return 440.0 * 2 ** (semitones_from_a4 / 12.0)


def radio(melody, bass, bpm, drums, seconds_per_bar=None):
    beat = 60.0 / bpm
    steps = len(melody)
    step_len = beat / 2  # eighth notes
    n = int(steps * step_len * RATE)
    out = [0.0] * n
    for step, semis in enumerate(melody):
        if semis is None:
            continue
        start = int(step * step_len * RATE)
        length = int(step_len * 1.6 * RATE)
        freq = note_hz(semis)
        for i in range(length):
            j = start + i
            if j >= n:
                break
            t = i / RATE
            env = min(1.0, t * 80) * math.exp(-t * 4.5)
            out[j] += env * (0.6 * math.sin(2 * math.pi * freq * t) + 0.25 * math.sin(4 * math.pi * freq * t))
    for step, semis in enumerate(bass):
        if semis is None:
            continue
        start = int(step * beat * 2 * RATE)
        length = int(beat * 2 * RATE)
        freq = note_hz(semis)
        for i in range(length):
            j = start + i
            if j >= n:
                break
            t = i / RATE
            out[j] += 0.5 * math.exp(-t * 1.5) * math.sin(2 * math.pi * freq * t)
    for step, hit in enumerate(drums):
        if not hit:
            continue
        start = int(step * step_len * RATE)
        for i in range(int(0.12 * RATE)):
            j = start + i
            if j >= n:
                break
            t = i / RATE
            if hit == "dum":  # low darbuka / kick
                out[j] += 0.9 * math.exp(-t * 28) * math.sin(2 * math.pi * 95 * t)
            else:  # "tek": bright slap
                out[j] += 0.35 * math.exp(-t * 60) * rng.uniform(-1, 1)
    return out


A_MINOR = [0, 3, 7, 12, 7, 3, 0, 3, -2, 2, 5, 10, 5, 2, -2, 2, -4, 0, 3, 8, 3, 0, -4, 0, -5, -1, 2, 7, 2, -1, -5, -1]
TEL_AVIR_FM = radio([m + 12 for m in A_MINOR], [-24, -26, -28, -29], 104,
                    ["dum", None, "tek", None] * 8)
# Hijaz on E: E F G# A B C D E
HIJAZ = [-5, -4, -1, 0, 2, 3, 2, 0, -1, -4, -5, None, -5, -1, 0, 2, 3, 5, 7, 5, 3, 2, 0, -1, 0, -1, -4, -5, -4, -5, None, None]
MIZRAHI_WAVE = radio([None if h is None else h + 12 for h in HIJAZ], [-29, -29, -31, -29], 96,
                     ["dum", None, "tek", "tek", "dum", "tek", None, "tek"] * 4)


def city_ambience():
    seconds = 10.0
    n = int(seconds * RATE) + int(0.2 * RATE)
    out, brown = [], 0.0
    for i in range(n):
        brown = (brown + rng.uniform(-1, 1) * 0.02) * 0.995
        out.append(brown)
    out = lowpass(out, 500)
    return crossfade_loop(out, 0.2)


def blip(freq, seconds, decay):
    n = int(seconds * RATE)
    return [math.sin(2 * math.pi * freq * (i / RATE)) * math.exp(-(i / RATE) * decay) for i in range(n)]


SOUNDS = {
    "S_Shot_Pistol": (gunshot(0.6, 26, 18, 120, 5500), False),
    "S_Shot_SMG": (gunshot(0.35, 38, 25, 150, 7000), False),
    "S_Shot_Shotgun": (gunshot(0.9, 12, 9, 80, 3500, echo=0.25), False),
    "S_Shot_Rifle": (gunshot(0.7, 20, 14, 105, 6500, echo=0.3), False),
    "S_Engine_Loop": (engine_loop(), True),
    "S_Siren_Loop": (siren_loop(), True),
    "S_Radio_TelAvirFM": (TEL_AVIR_FM, True),
    "S_Radio_MizrahiWave": (MIZRAHI_WAVE, True),
    "S_City_Ambience": (city_ambience(), True),
    "S_UI_Click": (blip(1800, 0.05, 90), False),
    "S_UI_HitMarker": (blip(3200, 0.04, 120), False),
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
tasks = []
for name, (samples, looping) in SOUNDS.items():
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", write_wav(name, samples))
    task.set_editor_property("destination_path", FOLDER)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tasks.append(task)
asset_tools.import_asset_tasks(tasks)

for name, (samples, looping) in SOUNDS.items():
    path = f"{FOLDER}/{name}"
    wave_asset = unreal.EditorAssetLibrary.load_asset(path)
    if wave_asset:
        wave_asset.set_editor_property("looping", looping)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        unreal.log_warning(f"[Mishpacha] sound {path} (loop={looping})")
    else:
        unreal.log_error(f"[Mishpacha] failed to import {name}")
