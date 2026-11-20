#!/usr/bin/env python3
"""
Converts an SFZ multisample instrument into a compact C++ header for
TinyInstruments, so that it fits into the flash memory (PROGMEM) of a
microcontroller like the ESP32.

Size reduction:
 - mono, resampled to --rate (default 32 kHz)
 - each zone is reduced to the attack + a short, crossfaded sustain loop.
   The natural decay is measured, removed from the loop and stored as a
   parameter, so that the player can re-apply it as an envelope.
 - 4 bit IMA ADPCM (4:1 compared to 16 bit PCM)
 - the zone lengths are scaled automatically to fit into --budget bytes

Requirements: python3, numpy, ffmpeg (for decoding flac/wav/ogg)

Example:
  python3 tools/sfz2header.py UprightPianoKW-20220221.sfz \
      --name UprightPianoKW --budget 700000 -o src/samples/UprightPianoKW.h
"""

import argparse
import math
import os
import re
import subprocess
import sys

import numpy as np

# ---------------------------------------------------------------------------
# SFZ parsing
# ---------------------------------------------------------------------------

NOTE_NAMES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def parse_key(value):
    """SFZ keys can be numbers or note names like c#4 (c4 = 60)."""
    try:
        return int(value)
    except ValueError:
        m = re.match(r"([a-gA-G])([#b]?)(-?\d+)$", value)
        if not m:
            raise ValueError("invalid key: " + value)
        n = NOTE_NAMES[m.group(1).lower()]
        if m.group(2) == "#":
            n += 1
        elif m.group(2) == "b":
            n -= 1
        return n + (int(m.group(3)) + 1) * 12


def preprocess_sfz(path, base_dir=None, defines=None, depth=0):
    """Resolves #include (relative to the main file) and #define."""
    if depth > 20:
        sys.exit("#include nesting too deep")
    base_dir = base_dir or os.path.dirname(os.path.abspath(path))
    defines = defines if defines is not None else {}
    text = open(path, encoding="utf-8", errors="replace").read()
    text = re.sub(r"//.*", "", text)
    out = []
    for line in text.splitlines():
        m = re.match(r'\s*#define\s+(\$\w+)\s+(\S+)', line)
        if m:
            defines[m.group(1)] = m.group(2)
            continue
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        if m:
            inc = os.path.join(base_dir, m.group(1))
            out.append(preprocess_sfz(inc, base_dir, defines, depth + 1))
            continue
        for name in sorted(defines, key=len, reverse=True):
            line = line.replace(name, defines[name])
        out.append(line)
    return "\n".join(out)


def parse_sfz(path):
    """Returns the list of regions as dicts (with inherited header opcodes).
    Regions which depend on midi controllers are evaluated with the default
    controller values (set_ccN, otherwise 0)."""
    text = preprocess_sfz(path)
    tokens = re.findall(r"<\w+>|sample=[^\n\r]+?(?=\s+\w+=|\s*<|\s*$)|\S+?=\S+", text, re.M)
    scopes = {"global": {}, "master": {}, "group": {}}
    control = {}
    current = None
    regions = []
    for tok in tokens:
        if tok.startswith("<"):
            header = tok[1:-1]
            if header == "region":
                current = {}
                current.update(scopes["global"])
                current.update(scopes["master"])
                current.update(scopes["group"])
                regions.append(current)
            elif header in scopes:
                if header == "global":
                    scopes["master"] = {}
                    scopes["group"] = {}
                if header == "master":
                    scopes["group"] = {}
                scopes[header] = {}
                current = scopes[header]
            elif header == "control":
                current = control
            else:
                current = {}  # <curve> ...: ignored
            continue
        key, value = tok.split("=", 1)
        if current is not None:
            current[key.strip()] = value.strip()

    cc = {}
    for key, value in control.items():
        m = re.match(r"set_cc(\d+)$", key)
        if m:
            cc[int(m.group(1))] = float(value)

    def active(r):
        if r.get("trigger", "attack") not in ("attack", "first"):
            return False
        for key, value in r.items():
            m = re.match(r"(lo|hi)cc(\d+)$", key)
            if m:
                v = cc.get(int(m.group(2)), 0)
                if m.group(1) == "lo" and v < float(value):
                    return False
                if m.group(1) == "hi" and v > float(value):
                    return False
        return True

    result = []
    base = os.path.dirname(os.path.abspath(path))
    default_path = control.get("default_path", scopes["global"].get("default_path", ""))
    for r in regions:
        if "sample" not in r or not active(r):
            continue
        if r["sample"].startswith("*"):
            continue  # generated sounds like *silence
        key = r.get("key")
        lokey = parse_key(r.get("lokey", key if key else "0"))
        hikey = parse_key(r.get("hikey", key if key else "127"))
        # SFZ: key= sets the root key, otherwise the default of pitch_keycenter is 60
        center = parse_key(r.get("pitch_keycenter", key if key else "60"))
        sample = r["sample"].replace("\\", "/")
        result.append({
            "file": os.path.join(base, default_path, sample),
            "lokey": lokey,
            "hikey": hikey,
            "root": center,
            "lovel": int(r.get("lovel", 1)),
            "hivel": int(r.get("hivel", 127)),
            "offset": int(r.get("offset", 0)),
            "end": int(r.get("end", 0)),
            "group": int(r.get("group", 0)),
            "off_by": int(r.get("off_by", 0)),
            # round robin / random alternatives
            "rr": int(r.get("seq_position", 1)) > 1 or float(r.get("lorand", 0)) > 0,
        })
    return result


# ---------------------------------------------------------------------------
# Audio processing
# ---------------------------------------------------------------------------

def load_mono(path, rate, offset=0, end=0):
    """Decodes to mono float at rate; offset and end in samples of the source."""
    trim = "atrim=start_sample=%d" % offset
    if end > offset:
        trim += ":end_sample=%d" % (end + 1)
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-ac", "1",
           "-af", "%s,aresample=%d" % (trim, rate), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def trim_start(x, rate):
    peak = np.max(np.abs(x))
    idx = np.argmax(np.abs(x) > peak * 0.01)
    idx = max(0, idx - int(0.001 * rate))
    return x[idx:]


def rms_db(x, rate, frame_s=0.01):
    n = int(frame_s * rate)
    frames = len(x) // n
    f = x[: frames * n].reshape(frames, n)
    rms = np.sqrt(np.mean(f * f, axis=1)) + 1e-9
    return 20 * np.log10(rms), n


def measure_decay(x, rate, start, duration_s=2.5, range_db=0.0):
    """Decay in dB per second measured from start (linear regression). If
    range_db > 0 the measurement stops when the level has dropped by range_db
    (avoids measuring the noise floor)."""
    db, n = rms_db(x, rate)
    a = start // n
    b = min(len(db), a + int(duration_s / 0.01))
    if range_db > 0 and a < len(db):
        below = np.nonzero(db[a:b] < db[a] - range_db)[0]
        if len(below) > 0:
            b = a + max(int(below[0]), 10)
    if b - a < 10:
        return -6.0
    t = np.arange(a, b) * 0.01
    slope = np.polyfit(t, db[a:b], 1)[0]
    return float(np.clip(slope, -80.0, -0.3))


def find_loop_start(y, loop_end, approx_start, rate, search_s=0.08, window_s=0.03):
    """Finds the loop start whose preceding signal matches best the signal
    preceding the loop end (normalized cross correlation)."""
    w = int(window_s * rate)
    r = int(search_s * rate)
    target = y[loop_end - w: loop_end]
    lo = max(w, approx_start - r)
    hi = min(loop_end - w - 1, approx_start + r)
    segs = np.lib.stride_tricks.sliding_window_view(y[lo - w: hi], w)
    num = segs @ target
    den = np.sqrt(np.sum(segs * segs, axis=1) * np.dot(target, target)) + 1e-12
    ncc = num / den
    best = int(np.argmax(ncc))
    return lo + best, float(ncc[best])


def loop_region(length, rate, loop_fraction):
    loop_len = int(np.clip(loop_fraction * length, 0.1 * rate, 0.5 * rate))
    loop_len = min(loop_len, int(length * 0.6))
    return length - loop_len


def smooth_decays(regions, decays):
    """Median of the neighbors in the same velocity layer: removes outliers
    caused by beating or noise."""
    result = list(decays)
    for i, r in enumerate(regions):
        layer = [j for j, o in enumerate(regions) if o["lovel"] == r["lovel"]]
        k = layer.index(i)
        values = [decays[j] for j in layer[max(0, k - 1): k + 2]]
        result[i] = float(np.median(values))
    return result


def loop_slope(y, rate, loop_start, loop_end):
    """Level trend inside of the loop in dB per second (linear regression of
    the 10 ms levels); 0 if the loop is too short to measure it."""
    if loop_end - loop_start < int(0.08 * rate):
        return 0.0
    db, _ = rms_db(y[loop_start:loop_end], rate)
    t = np.arange(len(db)) * 0.01
    return float(np.polyfit(t, db, 1)[0])


def flatten_loop_envelope(y, rate, loop_start, loop_end, root, max_db=12.0):
    """Removes the level fluctuations inside of the loop (e.g. the beating of
    the strings of a piano): repeated with the loop they would become a
    periodic warble. The waveform is kept, only the envelope (measured over at
    least 3 periods) is made constant, at the level of the loop start, so the
    loop still continues the preceding signal."""
    f0 = 440.0 * 2 ** ((root - 69) / 12.0)
    w = max(int(0.02 * rate), int(3 * rate / f0))
    seg = y[loop_start:loop_end]
    if len(seg) < 4 * w:
        return
    kernel = np.ones(w) / w
    # mirrored at the ends: no level drop at the borders of the loop
    env = np.sqrt(np.convolve(np.pad(seg * seg, w, mode="reflect"), kernel, mode="same")[w:-w]) + 1e-9
    target = env[w // 2]
    gain = np.clip(target / env, 10 ** (-max_db / 20), 10 ** (max_db / 20))
    gain = np.convolve(np.pad(gain, w, mode="edge"), kernel, mode="same")[w:-w]
    gain *= 1.0 / gain[0]  # no step at the loop start
    y[loop_start:loop_end] = seg * gain


def process_zone(x, rate, length, loop_fraction, xfade_s, max_flatten_db, decay_db,
                 root=60, flat_envelope=False):
    """Returns (pcm float, loop_start, loop_end, decay_start, decay_db_per_s, ncc)."""
    approx_start = loop_region(length, rate, loop_fraction)
    y = x[:length].copy()
    loop_end = length
    loop_start, ncc = find_loop_start(y, loop_end, approx_start, rate)

    # remove the decay from the loop start on: amplitude becomes constant,
    # the player re-applies the decay from decay_start = loop_start
    n = np.arange(length - loop_start)
    gain_db = np.minimum(-decay_db * n / rate, max_flatten_db)
    y[loop_start:] *= 10 ** (gain_db / 20.0)

    # the decay is measured over a long time (and smoothed between the keys),
    # but e.g. a piano decays faster at the beginning, where the loop is: the
    # rest of the decay inside of the loop would restart with each repetition
    # (a periodic level step which sounds like a vibrato). Remove it as well.
    residual = loop_slope(y, rate, loop_start, loop_end)
    if residual != 0.0:
        corr_db = np.clip(-residual * n / rate, -max_flatten_db, max_flatten_db)
        y[loop_start:] *= 10 ** (corr_db / 20.0)
    if flat_envelope:
        flatten_loop_envelope(y, rate, loop_start, loop_end, root)

    # crossfade the end of the loop into the signal preceding the loop start:
    # the less correlated, the longer the crossfade and the more equal power
    loop_len = loop_end - loop_start
    xf_s = xfade_s + (1.0 - max(ncc, 0.0)) * 0.25 * loop_len / rate
    xf = min(int(xf_s * rate), loop_start - approx_start // 2, loop_len // 2)
    if xf > 1:
        t = (np.arange(xf) + 1) / xf
        p = float(np.clip(ncc, 0.0, 1.0))
        fade_in = p * t + (1 - p) * np.sin(t * np.pi / 2)
        fade_out = p * (1 - t) + (1 - p) * np.cos(t * np.pi / 2)
        y[loop_end - xf: loop_end] = (y[loop_end - xf: loop_end] * fade_out
                                      + y[loop_start - xf: loop_start] * fade_in)
    return y, loop_start, loop_end, loop_start, decay_db, ncc


def estimate_cents(y, rate, root):
    """Deviation of the pitch from the nominal pitch of the root key in cents
    (autocorrelation around the expected period), 0 if not measurable."""
    f0 = 440.0 * 2 ** ((root - 69) / 12.0)
    period = rate / f0
    # search +/- 150 cents: also finds a root key which is off by a semitone
    lo = int(period * 2 ** (-150 / 1200.0)) - 1
    hi = int(period * 2 ** (150 / 1200.0)) + 2
    n = min(len(y), 16384)
    if n < hi * 4:
        return 0.0
    x = y[:n] - np.mean(y[:n])
    spec = np.fft.rfft(x, 2 * n)
    ac = np.fft.irfft(spec * np.conj(spec))[: hi + 2]
    lag = lo + int(np.argmax(ac[lo:hi]))
    if lag <= 0 or lag + 1 >= len(ac):
        return 0.0
    # octave check: a strong peak at half the period means that the sound
    # is an octave higher than the root key
    half = lag // 2
    ac_full = np.fft.irfft(spec * np.conj(spec))
    if half > 2 and np.max(ac_full[half - 2: half + 3]) > 0.8 * ac[lag]:
        print("WARNING: the pitch seems to be an octave above the root key %d" % root)
    double = 2 * lag
    if double + 3 < len(ac_full) and np.max(ac_full[double - 2: double + 3]) > 1.25 * ac[lag]:
        print("WARNING: the pitch seems to be an octave below the root key %d" % root)
    y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
    denom = y0 - 2 * y1 + y2
    shift = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
    measured = rate / (lag + shift)
    cents = 1200 * math.log2(measured / f0)
    if abs(cents) > 60:
        print("WARNING: root key %d is off by %+.0f cents (wrong pitch_keycenter?)" % (root, cents))
    return float(np.clip(cents, -127, 127))


def pitch_marks(y, rate, f0, max_pos=None):
    """Start of each period (for PSOLA): the first mark is at the end of the
    attack, the next ones are found by correlation with the previous period,
    so that all marks have the same phase."""
    period = rate / f0
    n = len(y) if max_pos is None else min(len(y), max_pos)
    # emphasize the fundamental: low pass at 1.5 x f0
    spec = np.fft.rfft(y[:n])
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    low = np.fft.irfft(spec / (1.0 + (freqs / (1.5 * f0)) ** 4), n)
    env = np.abs(low)
    peak = np.max(env)
    if peak <= 0:
        return []
    onset = int(np.argmax(env > 0.3 * peak))
    # first mark: positive peak of the fundamental within one period after the onset
    start = onset + int(period)
    if start + 3 * period >= n:
        return []
    first = start + int(np.argmax(low[start: start + int(period)]))
    marks = [first]
    half = int(period / 2)
    while True:
        t = marks[-1]
        lo, hi = int(t + 0.8 * period), int(t + 1.2 * period) + 1
        if hi + half >= n or t - half < 0:
            break
        template = low[t - half: t + half]
        candidates = np.lib.stride_tricks.sliding_window_view(low[lo - half: hi + half], 2 * half)
        best = lo + int(np.argmax(candidates @ template))
        marks.append(best)
    return marks


def playback(dec, rate, length, ls, le, ds, decay_ln, seconds):
    """What the player outputs for the zone (without pitch shifting): the
    loop is repeated and the decay envelope is applied."""
    n = int(seconds * rate)
    idx = np.arange(n)
    if le > ls:
        src = np.where(idx < le, idx, ls + (idx - le) % max(le - ls, 1))
        out = dec[np.minimum(src, len(dec) - 1)].astype(np.float64)
    else:
        out = np.zeros(n)
        k = min(n, length)
        out[:k] = dec[:k]
    if decay_ln < 0 and ds < n:
        out[ds:] *= np.exp(decay_ln * np.arange(n - ds))
    return out


def quality(reference, ref_rate, recon, rec_rate, seconds=1.5, max_freq=16000):
    """Compares the reconstruction with the original recording: spectral
    difference (RMS difference of the third octave band levels 50 Hz - 16 kHz
    in dB, after matching the overall level, like a spectrum analyzer: the
    perceived timbre) and envelope difference (mean of the frame level
    differences in dB). Both are averaged over the frames."""
    n = min(int(seconds * ref_rate), len(reference))
    ref = reference[:n]
    t_ref = np.arange(n) / ref_rate
    t_rec = np.arange(len(recon)) / rec_rate
    rec = np.interp(t_ref, t_rec, recon, right=0.0)
    # level matching on the whole window
    gain = np.sqrt(np.sum(ref * ref) / max(np.sum(rec * rec), 1e-12))
    rec = rec * gain
    size, hop = 2048, 1024
    if n < size * 2:
        return 0.0, 0.0
    win = np.hanning(size)
    freqs = np.fft.rfftfreq(size, 1.0 / ref_rate)
    edges = [50.0 * 2 ** (k / 3.0) for k in range(0, 31)]
    edges = [e for e in edges if e <= min(max_freq, ref_rate / 2)]
    bands = [(freqs >= lo) & (freqs < hi) for lo, hi in zip(edges[:-1], edges[1:])]
    bands = [b for b in bands if np.any(b)]
    spec, env = [], []
    frames = range(0, n - size, hop)

    def band_levels(x):
        power = np.abs(np.fft.rfft(x * win)) ** 2
        return np.array([10 * np.log10(np.sum(power[b]) + 1e-12) for b in bands])

    ref_frames = [band_levels(ref[i:i + size]) for i in frames]
    rec_frames = [band_levels(rec[i:i + size]) for i in frames]
    peak_db = max(np.max(f) for f in ref_frames)
    floor = peak_db - 60.0
    levels_ref = [10 * np.log10(np.mean(ref[i:i + size] ** 2) + 1e-12) for i in frames]
    levels_rec = [10 * np.log10(np.mean(rec[i:i + size] ** 2) + 1e-12) for i in frames]
    level_floor = max(levels_ref) - 60.0
    for a, b, la, lb in zip(ref_frames, rec_frames, levels_ref, levels_rec):
        a = np.maximum(a, floor)
        b = np.maximum(b, floor)
        spec.append(np.sqrt(np.mean((a - b) ** 2)))
        env.append(abs(max(la, level_floor) - max(lb, level_floor)))
    return float(np.mean(spec)), float(np.mean(env))


def shift_resample(x, ratio):
    """Pitch shift by playing faster (changes the length)."""
    pos = np.arange(0, len(x) - 1, ratio)
    return np.interp(pos, np.arange(len(x)), x)


def shift_psola(x, rate, f0, ratio):
    """Pitch shift with TD-PSOLA (keeps the length), like the player: the
    attack before the first mark is unchanged."""
    marks = pitch_marks(x, rate, f0)
    if len(marks) < 4:
        return x.copy()
    out = np.zeros(len(x))
    weight = np.zeros(len(x))
    # attack: unchanged up to mark 0, crossfade to mark 1
    m0, m1 = marks[0], marks[1]
    w = np.ones(m1)
    fade = np.arange(m1 - m0) / max(m1 - m0, 1)
    w[m0:m1] = 1 - fade * fade * (3 - 2 * fade)
    out[:m1] += x[:m1] * w
    weight[:m1] += w
    t = float(m1)
    k = 1
    while True:
        # mark closest to the synthesis time
        while k + 1 < len(marks) - 1 and abs(marks[k + 1] - t) <= abs(marks[k] - t):
            k += 1
        if k + 1 >= len(marks):
            break
        left, right = marks[k] - marks[k - 1], marks[k + 1] - marks[k]
        c = int(round(t))
        if c + right >= len(x) or marks[k] + right >= len(x):
            break
        a = np.arange(left) / left
        b = np.arange(right) / right
        win = np.concatenate([a * a * (3 - 2 * a), 1 - b * b * (3 - 2 * b)])
        grain = x[marks[k] - left: marks[k] + right]
        lo = c - left
        if lo >= 0:
            out[lo: c + right] += grain * win
            weight[lo: c + right] += win
        t += right / ratio  # the following period: exact marks for ratio 1
    return np.where(weight > 1e-3, out / np.maximum(weight, 1e-3), 0.0)


def natural_length(x, rate, threshold_db=50.0):
    """Number of samples until the sound has decayed by threshold_db."""
    db, n = rms_db(x, rate)
    above = np.nonzero(db > np.max(db) - threshold_db)[0]
    if len(above) == 0:
        return len(x)
    return min(len(x), (above[-1] + 1) * n)


def reduce_layers(regions, max_layers, indices=None):
    """Keeps max_layers evenly spaced velocity layers (or the layers with the
    indicated indices) per key range and extends their velocity ranges, so
    that 1..127 is still covered."""
    by_keys = {}
    for r in regions:
        by_keys.setdefault((r["lokey"], r["hikey"]), []).append(r)
    result = []
    for rs in by_keys.values():
        rs.sort(key=lambda r: r["lovel"])
        if indices:
            rs = [rs[i] for i in indices if i < len(rs)]
        elif len(rs) > max_layers:
            if max_layers == 1:
                rs = [rs[-1]]
            else:
                idx = sorted(set(round(i * (len(rs) - 1) / (max_layers - 1)) for i in range(max_layers)))
                rs = [rs[i] for i in idx]
        lovel = 1
        for i, r in enumerate(rs):
            r = dict(r)
            r["lovel"] = lovel
            if i == len(rs) - 1:
                r["hivel"] = 127
            lovel = r["hivel"] + 1
            result.append(r)
    return result


def thin_keys(regions, step):
    """Keeps only root keys which are at least `step` semitones apart (starting
    with the lowest one) and extends the key ranges of the kept regions to the
    middle between their neighbours, so that all keys are still covered."""
    roots = sorted(set(r["root"] for r in regions))
    keep = []
    for root in roots:
        if not keep or root - keep[-1] >= step:
            keep.append(root)
    # the key ranges of all regions (also the removed ones) per velocity range
    # (layers can cover different keys): the kept regions cover them
    bounds = {}
    for r in regions:
        vel = (r["lovel"], r["hivel"])
        lo, hi = bounds.get(vel, (r["lokey"], r["hikey"]))
        bounds[vel] = (min(lo, r["lokey"]), max(hi, r["hikey"]))
    regions = [r for r in regions if r["root"] in keep]
    by_vel = {}
    for r in regions:
        by_vel.setdefault((r["lovel"], r["hivel"]), []).append(r)
    result = []
    for vel, rs in by_vel.items():
        rs = sorted(rs, key=lambda r: r["root"])
        lowest, highest = bounds[vel]
        for i, r in enumerate(rs):
            r = dict(r)
            r["lokey"] = lowest if i == 0 else (rs[i - 1]["root"] + r["root"]) // 2 + 1
            r["hikey"] = highest if i == len(rs) - 1 else (r["root"] + rs[i + 1]["root"]) // 2
            result.append(r)
    print("--step %d: %d of %d root keys kept: %s" % (step, len(keep), len(roots), keep))
    return result


def apply_choke(regions, spec):
    """Defines the choke groups from a spec like '46:42,44;49:57'."""
    for r in regions:
        r["group"] = 0
        r["off_by"] = 0
    for n, rule in enumerate(spec.split(";")):
        target, chokers = rule.split(":")
        group = 1000 + n
        for r in regions:
            if r["root"] == int(target):
                r["off_by"] = group
            if r["root"] in [int(k) for k in chokers.split(",")]:
                if r["group"] not in (0, group):
                    sys.exit("key %d is in several choke rules" % r["root"])
                r["group"] = group


def normalize_layers(regions, sources):
    """Scales the layers of each key to the peak of the loudest layer."""
    peaks = [float(np.max(np.abs(x))) + 1e-9 for x in sources]
    by_key = {}
    for i, r in enumerate(regions):
        by_key.setdefault(r["root"], []).append(i)
    for idx in by_key.values():
        loudest = max(peaks[i] for i in idx)
        for i in idx:
            sources[i] *= loudest / peaks[i]


def map_groups(regions):
    """Maps the sfz group numbers to 1..255 (0 = no group)."""
    ids = {}
    for r in regions:
        for key in ("group", "off_by"):
            g = r[key]
            if g != 0 and g not in ids:
                ids[g] = len(ids) + 1
    if len(ids) > 255:
        sys.exit("too many choke groups")
    for r in regions:
        r["group"] = ids.get(r["group"], 0)
        r["off_by"] = ids.get(r["off_by"], 0)


# ---------------------------------------------------------------------------
# IMA ADPCM
# ---------------------------------------------------------------------------

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_encode(pcm, capture_at):
    """Encodes int16 samples. Returns (bytes, {index: (predictor, step_index)})
    with the decoder state before the sample at each index in capture_at."""
    pred, index = 0, 0
    out = bytearray((len(pcm) + 1) // 2)
    states = {}
    capture = set(capture_at)
    for i, s in enumerate(pcm.tolist()):
        if i in capture:
            states[i] = (pred, index)
        step = STEP_TABLE[index]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            delta += step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
            delta += step
        step >>= 1
        if diff >= step:
            code |= 1
            delta += step
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        if i & 1:
            out[i >> 1] |= code << 4
        else:
            out[i >> 1] = code
    return bytes(out), states


def adpcm_decode(data, count):
    pred, index = 0, 0
    res = np.zeros(count, dtype=np.int32)
    for i in range(count):
        code = (data[i >> 1] >> (4 if i & 1 else 0)) & 0xF
        step = STEP_TABLE[index]
        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        res[i] = pred
    return res


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def zone_length_s(root, bass_s, treble_s):
    """Target length: long for the bass, short for the treble (log interpolation)."""
    t = np.clip((root - 21) / (108 - 21), 0, 1)
    return math.exp(math.log(bass_s) * (1 - t) + math.log(treble_s) * t)


def write_header(path, name, rate, zones, source, license_text):
    guard_name = re.sub(r"\W", "_", name)
    total = sum(len(z["data"]) for z in zones)
    with open(path, "w") as f:
        f.write("#pragma once\n")
        f.write("// Generated by tools/sfz2header.py - do not edit\n")
        f.write("// Source: %s\n" % source)
        if license_text:
            f.write("// License of the samples: %s\n" % license_text)
        f.write("// %d zones, %d Hz, IMA ADPCM, %d bytes of sample data\n\n" % (len(zones), rate, total))
        f.write('#include "TinyInstruments/SampleSet.h"\n\n')
        f.write("namespace tinyinstruments {\n\n")
        f.write("/// Number of bytes of sample data in %s()\n" % guard_name)
        f.write("static const uint32_t %s_DATA_SIZE = %d;\n\n" % (guard_name.upper(), total))
        f.write("/// Sample set: %s\n" % name)
        f.write("inline const SampleSet& %s() {\n" % guard_name)
        for i, z in enumerate(zones):
            f.write("  static const uint8_t data%d[] TI_PROGMEM = {\n" % i)
            d = z["data"]
            for o in range(0, len(d), 40):
                f.write("    " + ",".join(str(b) for b in d[o:o + 40]) + ",\n")
            f.write("  };\n")
            if z.get("marks"):
                f.write("  static const uint16_t marks%d[] TI_PROGMEM = {\n" % i)
                ms = z["marks"]
                for o in range(0, len(ms), 24):
                    f.write("    " + ",".join(str(m) for m in ms[o:o + 24]) + ",\n")
                f.write("  };\n")
        f.write("  static const SampleZone zones[] TI_PROGMEM = {\n")
        for i, z in enumerate(zones):
            marks = ("marks%d, %d" % (i, len(z["marks"]))) if z.get("marks") else "nullptr, 0"
            f.write("    {data%d, %d, %d, %d, %d, %d, %d, %.9ef, %d, %d, %d, %d, %d, %d, %d, %d, %s},  // %s\n" % (
                i, z["length"], z["loop_start"], z["loop_end"], z["loop_pred"], z["loop_index"],
                z["decay_start"], z["decay_ln"], z["root"], z["lokey"], z["hikey"], z["lovel"],
                z["hivel"], z["group"], z["off_by"], z.get("tune", 0), marks, os.path.basename(z["file"])))
        f.write("  };\n")
        f.write('  static const SampleSet set = {"%s", zones, %d, %d, %d};\n' % (name, len(zones), rate, total))
        f.write("  return set;\n")
        f.write("}\n\n")
        f.write("}  // namespace tinyinstruments\n")


BINARY_MAGIC = b"TISS"


def write_binary(path, name, rate, zones):
    """Writes the sample set as binary file which can be loaded at runtime
    (DynamicSampleSet). Little endian:
      header: magic "TISS", u16 version, u16 zone count, u32 sample rate,
              u32 data size, u8 name length, name
      zones:  32 bytes each: u32 length, u32 loop start, u32 loop end,
              i16 loop predictor, u8 loop index, u8 root key, u32 decay start,
              f32 decay ln, u8 lo key, u8 hi key, u8 lo vel, u8 hi vel,
              u8 group, u8 off by, i8 tune, u8 reserved
      marks:  version 2 only: for each zone u16 mark count, then the marks of
              all zones (u16: distance to the previous mark, the first one:
              position)
      data:   the ADPCM data of all zones in the same order
    Version 1 is written if there are no pitch marks.
    """
    import struct
    data_size = sum(len(z["data"]) for z in zones)
    name_bytes = name.encode("utf-8")[:255]
    has_marks = any(z.get("marks") for z in zones)
    with open(path, "wb") as f:
        f.write(BINARY_MAGIC)
        f.write(struct.pack("<HHIIB", 2 if has_marks else 1, len(zones), rate, data_size, len(name_bytes)))
        f.write(name_bytes)
        for z in zones:
            f.write(struct.pack("<IIIhBBIfBBBBBBbB", z["length"], z["loop_start"], z["loop_end"],
                                z["loop_pred"], z["loop_index"], z["root"], z["decay_start"],
                                z["decay_ln"], z["lokey"], z["hikey"], z["lovel"], z["hivel"],
                                z["group"], z["off_by"], z.get("tune", 0), 0))
        if has_marks:
            for z in zones:
                f.write(struct.pack("<H", len(z.get("marks") or [])))
            for z in zones:
                for m in z.get("marks") or []:
                    f.write(struct.pack("<H", m))
        for z in zones:
            f.write(z["data"])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("sfz")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--name", required=True, help="C++ function name of the sample set")
    p.add_argument("--rate", type=int, default=32000, help="sample rate of the stored samples")
    p.add_argument("--budget", type=int, default=700000, help="max bytes of sample data")
    p.add_argument("--bass", type=float, default=1.6, help="relative length of the lowest zone (s)")
    p.add_argument("--treble", type=float, default=0.4, help="relative length of the highest zone (s)")
    p.add_argument("--loop-fraction", type=float, default=0.4)
    p.add_argument("--xfade", type=float, default=0.03, help="loop crossfade in seconds")
    p.add_argument("--max-flatten-db", type=float, default=18.0)
    p.add_argument("--natural-loop-envelope", action="store_true",
                   help="keyboard mode: keep the level fluctuations (e.g. beating) inside of "
                        "the loop (default: they are removed, they would become a periodic warble)")
    p.add_argument("--license", default="", help="license note written into the header")
    p.add_argument("--wav-dir", help="write the decoded result of each zone as wav for listening")
    p.add_argument("--mode", choices=["keyboard", "natural", "sustain"], default="keyboard",
                   help="keyboard: zone length from --bass to --treble (pianos, mallets); "
                        "natural: zone length from the sound itself, short sounds are played "
                        "as one shot, long ones are looped (drums, percussion); "
                        "sustain: same length for all zones, the loop holds a constant "
                        "level as long as the key is pressed (wind instruments, organs)")
    p.add_argument("--binary", help="also write the sample set as binary file (e.g. data/X.bin)")
    p.add_argument("--pitch-marks", action="store_true",
                   help="store the start of each period for pitch shifting with PSOLA "
                        "(which keeps the length of the sound); counts to the budget")
    p.add_argument("--quality-max-freq", type=float, default=16000,
                   help="upper frequency of the quality measurement, e.g. 5000 for instruments "
                        "which are played through a guitar amplifier")
    p.add_argument("--quality", action="store_true",
                   help="compare the playback of each zone with the original recording (44.1 kHz) "
                        "and report the spectral and envelope difference in dB")
    p.add_argument("--tune-correct", action="store_true",
                   help="measure the pitch of each zone and store a correction in cents")
    p.add_argument("--max-layers", type=int, default=0, help="max velocity layers per key range")
    p.add_argument("--layer-index", help="velocity layers to keep, e.g. '1,2' (0 = softest)")
    p.add_argument("--single-layer", action="store_true",
                   help="keep only the loudest velocity layer of each key (the player varies the "
                        "brightness with the velocity)")
    p.add_argument("--transpose", type=int, default=0,
                   help="semitones added to the keys of all regions (e.g. -12 if the sfz maps an "
                        "octave above the sounding pitch)")
    p.add_argument("--natural-db", type=float, default=40.0,
                   help="natural mode: a sound ends when it has decayed by this (dB)")
    p.add_argument("--keep-round-robin", action="store_true",
                   help="keep the round robin alternatives (default: only the first)")
    p.add_argument("--keys", help="only use the regions with a root key in this range, e.g. 35-84")
    p.add_argument("--step", type=int, default=0,
                   help="keep only root keys which are at least this many semitones apart, "
                        "e.g. 12 = one zone per octave; the kept zones are transposed to "
                        "cover the keys in between (not for drums)")
    p.add_argument("--choke", help="replaces the sfz choke groups: 'key:key,key;...' e.g. "
                   "'46:42,44' = 46 is silenced by 42 and 44")
    p.add_argument("--normalize-layers", action="store_true",
                   help="scale all velocity layers of a key to the level of the loudest layer "
                        "(the player creates the dynamics from the velocity)")
    args = p.parse_args()

    regions = parse_sfz(args.sfz)
    if not args.keep_round_robin:
        regions = [r for r in regions if not r["rr"]]
    if args.keys:
        lo, hi = (int(v) for v in args.keys.split("-"))
        regions = [r for r in regions if lo <= r["root"] <= hi]
    if args.transpose:
        for r in regions:
            for key in ("root", "lokey", "hikey"):
                r[key] = max(0, min(127, r[key] + args.transpose))
    if args.layer_index:
        regions = reduce_layers(regions, 0, [int(v) for v in args.layer_index.split(",")])
    elif args.max_layers > 0:
        regions = reduce_layers(regions, args.max_layers)
    if args.single_layer:
        regions = [dict(r, lovel=1) for r in regions if r["hivel"] == 127]
    before_thinning = list(regions)
    if args.step > 1:
        regions = thin_keys(regions, args.step)
    if not regions:
        sys.exit("no regions found")
    if args.choke:
        apply_choke(regions, args.choke)
    map_groups(regions)
    regions.sort(key=lambda r: (r["lovel"], r["root"]))

    sources = [trim_start(load_mono(r["file"], args.rate, r["offset"], r["end"]), args.rate)
               for r in regions]
    if args.normalize_layers:
        normalize_layers(regions, sources)

    # zone lengths which fit into the budget: 4 bits per sample, with pitch
    # marks 2 more bytes per period
    budget_samples = args.budget * 2
    if args.pitch_marks:
        f0s = [440.0 * 2 ** ((r["root"] - 69) / 12.0) for r in regions]
        mean_f0 = sum(f0s) / len(f0s)
        bytes_per_sample = 0.5 + 2.0 * mean_f0 / args.rate
        budget_samples = int(args.budget / bytes_per_sample)
    if args.mode in ("keyboard", "sustain"):
        if args.mode == "keyboard":
            base = [zone_length_s(r["root"], args.bass, args.treble) for r in regions]
        else:
            base = [1.0] * len(regions)
        scale = budget_samples / (sum(base) * args.rate)
        lengths = [min(int(b * scale * args.rate), len(x) - 1) & ~1 for b, x in zip(base, sources)]
        if args.mode == "keyboard":
            print("%d zones, length scale %.2f -> bass %.2fs, treble %.2fs" % (
                len(regions), scale, args.bass * scale, args.treble * scale))
        else:
            print("%d zones, zone length %.2fs" % (len(regions), scale))
    else:
        natural = [natural_length(x, args.rate, args.natural_db) for x in sources]
        # max length so that all zones fit into the budget (binary search)
        lo, hi = 0, max(natural)
        while hi - lo > 16:
            mid = (lo + hi) // 2
            if sum(min(n, mid) for n in natural) <= budget_samples:
                lo = mid
            else:
                hi = mid
        if lo < 0.2 * args.rate:
            sys.exit("budget too small: max zone length would be %.2fs" % (lo / args.rate))
        lengths = [min(n, lo) & ~1 for n in natural]
        print("%d zones, max zone length %.2fs" % (len(regions), lo / args.rate))

    decays = []
    range_db = 30.0 if args.mode == "natural" else 0.0
    for x, length in zip(sources, lengths):
        decays.append(measure_decay(x, args.rate, loop_region(length, args.rate, args.loop_fraction),
                                    range_db=range_db))
    if args.mode == "keyboard":
        decays = smooth_decays(regions, decays)
    elif args.mode == "natural":
        # the looped tail must have decayed by natural_db at the natural end
        # of the sound (e.g. a whistle which stops abruptly must not ring)
        for i, (n, length) in enumerate(zip(natural, lengths)):
            remaining_s = max(n - loop_region(length, args.rate, args.loop_fraction), 0.1 * args.rate) / args.rate
            decays[i] = min(decays[i], -args.natural_db / remaining_s)

    processed = []
    peak = 0.0
    for r, x, length, decay in zip(regions, sources, lengths, decays):
        if args.mode == "natural" and length < lo - 16:
            # complete sound: one shot without loop, short fade out at the end
            y = x[:length].copy()
            fade = min(len(y) // 4, int(0.02 * args.rate))
            y[len(y) - fade:] *= np.linspace(1.0, 0.0, fade)
            ls, le, ds, decay_db, ncc = 0, 0, length, 0.0, 1.0
        else:
            y, ls, le, ds, decay_db, ncc = process_zone(
                x, args.rate, length, args.loop_fraction, args.xfade, args.max_flatten_db, decay,
                root=r["root"], flat_envelope=args.mode == "keyboard" and not args.natural_loop_envelope)
        peak = max(peak, float(np.max(np.abs(y))))
        processed.append((r, y, ls, le, ds, decay_db, ncc))

    zones = []
    scale_pcm = 0.97 * 32767 / peak
    for r, y, ls, le, ds, decay_db, ncc in processed:
        pcm = np.clip(np.round(y * scale_pcm), -32768, 32767).astype(np.int32)
        marks = []
        if args.pitch_marks:
            cents = estimate_cents(y[ls:le], args.rate, r["root"]) if le > ls else 0.0
            f0 = 440.0 * 2 ** ((r["root"] - 69 + cents / 100.0) / 12.0)
            marks = pitch_marks(y, args.rate, f0, le if le > ls else len(y))
        data, states = adpcm_encode(pcm, [ls])
        decay_ln = decay_db / 20.0 * math.log(10) / args.rate  # per sample
        if args.mode == "sustain":
            decay_ln = 0.0  # the decay was only removed from the loop
        z = dict(r)
        if args.tune_correct and le > ls:
            if args.mode == "sustain":
                cents = estimate_cents(y[ls:le], args.rate, r["root"])
            else:
                # decaying sounds (e.g. plucked strings): the pitch drifts,
                # what is heard is the pitch after the attack
                a, b = int(0.1 * args.rate), int(0.5 * args.rate)
                cents = estimate_cents(y[a:min(b, len(y))], args.rate, r["root"]) if len(y) > b else \
                    estimate_cents(y[ls:le], args.rate, r["root"])
            if abs(cents) <= 50:
                z["tune"] = int(round(-cents))
            else:
                # more than a quarter tone: rather a wrong measurement (e.g.
                # octave error) than a wrong tuning
                print("WARNING: %s: %+.0f cents not corrected (implausible)" % (os.path.basename(r["file"]), cents))
        # marks as distances to the previous mark (the first one: position)
        z["marks"] = [m - (marks[k - 1] if k > 0 else 0) for k, m in enumerate(marks)]
        if any(d <= 0 or d > 65535 for d in z["marks"]):
            sys.exit("invalid pitch marks in %s" % r["file"])
        z.update(data=data, length=len(pcm), loop_start=ls, loop_end=le,
                 loop_pred=states[ls][0], loop_index=states[ls][1],
                 decay_start=ds, decay_ln=decay_ln)
        zones.append(z)
        if args.quality:
            dec = adpcm_decode(data, len(pcm)).astype(np.float64)
            rec = playback(dec, args.rate, len(pcm), ls, le, ds, decay_ln, 1.5)
            ref = trim_start(load_mono(r["file"], 44100, r["offset"], r["end"]), 44100)
            z["quality"] = quality(ref, 44100, rec, args.rate, max_freq=args.quality_max_freq)
        print("  %-12s root %3d vel %3d-%3d len %5.2fs loop %5.2f-%5.2fs ncc %.3f decay %6.1f dB/s tune %+d marks %d" % (
            os.path.basename(r["file"]), r["root"], r["lovel"], r["hivel"], len(pcm) / args.rate,
            ls / args.rate, le / args.rate, ncc, decay_db, z.get("tune", 0), len(marks)))
        if args.wav_dir:
            os.makedirs(args.wav_dir, exist_ok=True)
            dec = adpcm_decode(data, len(pcm)).astype(np.int16)
            write_wav(os.path.join(args.wav_dir, os.path.basename(r["file"]) + ".wav"), dec, args.rate)

    if args.quality:
        q = [z["quality"] for z in zones]
        print("quality: spectral difference %.2f dB, envelope difference %.2f dB (mean of %d zones)" % (
            np.mean([a for a, b in q]), np.mean([b for a, b in q]), len(q)))
        # notes which were dropped by --step: created from the closest zone
        kept = set((z["root"], z["lovel"]) for z in zones)
        dropped = [r for r in before_thinning if (r["root"], r["lovel"]) not in kept]
        if dropped:
            for method in ("resample", "psola"):
                results, shifts = [], []
                for d in dropped:
                    zone = None
                    for z in zones:
                        if z["lovel"] <= d["lovel"] <= z["hivel"] and z["lokey"] <= d["root"] <= z["hikey"]:
                            zone = z
                    if zone is None:
                        continue
                    semis = d["root"] - zone["root"] + zone.get("tune", 0) / 100.0
                    ratio = 2 ** (semis / 12.0)
                    dec = adpcm_decode(zone["data"], zone["length"]).astype(np.float64)
                    if method == "resample":
                        play = playback(dec, args.rate, zone["length"], zone["loop_start"], zone["loop_end"],
                                        zone["decay_start"], zone["decay_ln"], 1.5 * ratio + 0.1)
                        shifted = shift_resample(play, ratio)
                    else:
                        play = playback(dec, args.rate, zone["length"], zone["loop_start"], zone["loop_end"],
                                        zone["decay_start"], zone["decay_ln"], 1.6)
                        f0 = 440.0 * 2 ** ((zone["root"] - 69 - zone.get("tune", 0) / 100.0) / 12.0)
                        shifted = shift_psola(play, args.rate, f0, ratio)
                    ref = trim_start(load_mono(d["file"], 44100, d["offset"], d["end"]), 44100)
                    results.append(quality(ref, 44100, shifted, args.rate, max_freq=args.quality_max_freq))
                    shifts.append(abs(semis))
                if results:
                    print("transposed (%s): spectral difference %.2f dB, envelope difference %.2f dB "
                          "(%d notes, up to %.1f semitones)" % (
                              method, np.mean([a for a, b in results]), np.mean([b for a, b in results]),
                              len(results), max(shifts)))
    write_header(args.output, args.name, args.rate, zones, os.path.basename(args.sfz), args.license)
    mark_count = sum(len(z.get("marks") or []) for z in zones)
    if mark_count:
        print("%d pitch marks: %d bytes" % (mark_count, mark_count * 2))
    if args.binary:
        write_binary(args.binary, args.name, args.rate, zones)
        print("written %s" % args.binary)
    total = sum(len(z["data"]) for z in zones)
    print("written %s: %d bytes of sample data + %d bytes pitch marks (budget %d)" % (
        args.output, total, mark_count * 2, args.budget))


def write_wav(path, pcm, rate):
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main()
