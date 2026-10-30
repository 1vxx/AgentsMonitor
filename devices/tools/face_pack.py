"""Makes a photo pack from one photo of a face: every clip in a theme's
expression table (devices/themes/<theme>/faces.py), rendered by LivePortrait
frame by frame from its parameters, in one file the display plays.

    python3 devices/tools/face_pack.py <photo> [--name xiaomei] [--theme her] [-o pack]

LivePortrait is found at $LIVEPORTRAIT, else ~/.cache/agentsmon/liveportrait,
and this runs again with the Python installed there, which has its packages.
Each face gets a pack of its own, named with --name (else after the photo's
file): devices/sim/build/<theme>-packs/<name>.pack, listed in index.json there
for the review page to pick from; build.sh --pack <name> writes one to a
board. Frames are kept under devices/sim/build/<theme>-frames/, a folder per
photo, so a face renders once: run again, or for a face made before, only the
clips whose parameters changed render, and the rest is packing, in seconds.
Nothing it writes is meant for the repo.

The pack, little-endian:
    "FPK2", u32 total length, u16 width, u16 height, u16 fps, u16 clip count,
        u32 offset and u32 length of the text section (0 without one)
    per clip: name (16 bytes, NUL padded), u16 first frame, u16 frame count
    u32 frame count, then per frame: u32 offset from the start, u32 length
    the frames, baseline JPEGs of the whole frame, each on a 4-byte boundary
    the text section, if the theme has a TEXT: glyphs to draw its words
        smooth with, a face per entry in the order TEXT has them:
        "FNT1", u16 face count, u16 0
        per face: u16 size, i16 ascent, i16 descent, u16 shadow blur,
            u32 glyph count, u32 offset of its glyphs (from the section start)
        per glyph, by code point: u32 code point, u32 offset of its bitmap
            (from the section start), u8 width, u8 height, i8 left of the box
            from the pen, i8 top of the box from the baseline, u8 advance,
            u8 layers, u16 0
        the bitmaps: width x height bytes of coverage, then as many again for
            the shadow when the glyph has two layers
(A pack starting "FPK1" is the same with neither the two u32s nor the text.)
"""

import argparse
import hashlib
import importlib.util
import json
import os
import struct
import sys
import time
from pathlib import Path

DEVICES = Path(__file__).resolve().parent.parent
SIZE = 466
QUALITY = 80

ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("photo")
ap.add_argument("--theme", default="her")
ap.add_argument("--name", help="whose face this is, the pack's name; the photo's file name if not given")
ap.add_argument("-o", "--out", help="a pack file to write instead of a named one")
ap.add_argument("--liveportrait", default=os.environ.get("LIVEPORTRAIT", str(Path.home() / ".cache/agentsmon/liveportrait")))
args = ap.parse_args()

# LivePortrait's own Python has torch and the rest: started with another, start
# again with that one. Its operations the Mac's GPU lacks fall back to the CPU.
os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
venv = Path(args.liveportrait) / ".venv"
if Path(sys.prefix).resolve() != venv.resolve():
    if not (venv / "bin" / "python").exists():
        sys.exit(f"LivePortrait isn't installed at {args.liveportrait} (set LIVEPORTRAIT, or --liveportrait)")
    os.execv(str(venv / "bin" / "python"), [str(venv / "bin" / "python"), __file__, *sys.argv[1:]])

spec = importlib.util.spec_from_file_location("faces", DEVICES / "themes" / args.theme / "faces.py")
F = importlib.util.module_from_spec(spec)
spec.loader.exec_module(F)
photo = Path(args.photo).resolve()
BUILD = DEVICES / "sim" / "build"
who = args.name or photo.stem
if any(c in who for c in '/\\:') or who.startswith("."):
    sys.exit(f"{who!r} won't do as a name: no slashes, colons or leading dot")
packs = BUILD / f"{args.theme}-packs"
out = Path(args.out).resolve() if args.out else packs / f"{who}.pack"

# ---------- The clips, as parameters per frame ----------


def toward(pose, k):
    """k of the way from rest to pose"""
    p = {key: v * k for key, v in pose.items() if key != "zoom"}
    if "zoom" in pose:
        p["zoom"] = 1 + (pose["zoom"] - 1) * k
    return p


def plus(a, b):
    p = dict(a)
    for key, v in b.items():
        p[key] = p.get(key, 1 if key == "zoom" else 0) + v
    return p


def ease(x):
    return x * x * (3 - 2 * x)


def frames(length):
    return [i / F.FPS for i in range(max(1, round(length * F.FPS)))]


def rise(pose, length):
    n = len(frames(length))
    return [toward(pose, ease((i + 1) / (n + 1))) for i in range(n)]


def fall(pose, length):
    return list(reversed(rise(pose, length)))


def alive(pose, motion, length):
    """At the peak, breathing, blinking once unless the eyes are shut"""
    seq = []
    for t in frames(length):
        p = plus(pose, motion(t) if motion else {})
        p = plus(p, dict(pitch=0.6 * F.wave(t, length), yaw=0.8 * F.wave(t, length, 0.3)))
        if pose.get("shut", 0) < 0.8:
            # A blink closes the eyes from wherever they are, wide open included
            b, s = F.blink(t, length * 0.7), p.get("shut", 0)
            p["shut"] = s + (1 - s) * b if s < 0 else max(s, b)
        seq.append(p)
    return seq


def once(pose, length):
    n = len(frames(length))
    up, down = round(n * 0.22), round(n * 0.3)
    return [toward(pose, ease(min(1, (i + 1) / (up + 1), (n - i) / (down + 1)))) for i in range(n)]


CLIPS = [("idle", [F.idle(t) for t in frames(F.IDLE_S)])]
for name, pose in F.STATES.items():
    CLIPS += [
        (name + ".in", rise(pose, F.IN_S)),
        (name + ".loop", alive(pose, F.MOTION.get(name), F.LOOP_S)),
        (name + ".out", fall(pose, F.OUT_S)),
    ]
for name, pose in F.REACTIONS.items():
    CLIPS.append((name, once(pose, F.REACT_S)))
for name, seq in CLIPS:
    if len(name) > 15:
        sys.exit(f"clip name too long for the pack: {name}")

# ---------- Rendering ----------

sys.path.insert(0, args.liveportrait)
here = os.getcwd()
os.chdir(args.liveportrait)  # its configs find the weights from here

import cv2  # noqa: E402
import numpy as np  # noqa: E402
import torch  # noqa: E402

from src.config.argument_config import ArgumentConfig  # noqa: E402
from src.config.crop_config import CropConfig  # noqa: E402
from src.config.inference_config import InferenceConfig  # noqa: E402
from src.gradio_pipeline import GradioPipeline  # noqa: E402
from src.utils.camera import get_rotation_matrix  # noqa: E402
from src.utils.crop import paste_back, prepare_paste_back  # noqa: E402
from src.utils.io import load_img_online  # noqa: E402
from src.utils.retargeting_utils import calc_eye_close_ratio, calc_lip_close_ratio  # noqa: E402


def fields(cls, d):
    return cls(**{k: v for k, v in d.items() if hasattr(cls, k)})


lpargs = ArgumentConfig()
pipe = GradioPipeline(fields(InferenceConfig, lpargs.__dict__), fields(CropConfig, lpargs.__dict__), lpargs)
lp = pipe.live_portrait_wrapper

with torch.no_grad():
    img = load_img_online(str(photo), mode="rgb", max_dim=1280, n=2)
    crop = pipe.cropper.crop_source_image(img, pipe.cropper.crop_cfg)
    if crop is None:
        sys.exit(f"no face found in {photo}")
    src = lp.prepare_source(crop["img_crop_256x256"])
    lmk = crop["lmk_crop"]
    info = lp.get_kp_info(src)
    feat = lp.extract_feature_3d(src)
    x_s = lp.transform_keypoint(info)
    eye0 = float(calc_eye_close_ratio(lmk[None]).mean())
    lips0 = float(calc_lip_close_ratio(lmk[None])[0][0])
    M = crop["M_c2o"]
    mask = prepare_paste_back(lp.inference_cfg.mask_crop, M, dsize=(img.shape[1], img.shape[0]))
    # The display gets a square round her face, cut from the whole photo
    cx, cy = (M[:2, :2] @ [256, 256] + M[:2, 2]).tolist()
    side = min(img.shape[0], img.shape[1])
    sx = int(min(max(cx - side / 2, 0), img.shape[1] - side))
    sy = int(min(max(cy - side / 2, 0), img.shape[0] - side))

EDITS = (
    ("smile", pipe.update_delta_new_smile),
    ("lip0", pipe.update_delta_new_lip_variation_zero),
    ("wink", pipe.update_delta_new_wink),
    ("eyebrow", pipe.update_delta_new_eyebrow),
    ("lip1", pipe.update_delta_new_lip_variation_one),
    ("lip3", pipe.update_delta_new_lip_variation_three),
)


@torch.no_grad()
def render(p):
    R = get_rotation_matrix(info["pitch"] + p.get("pitch", 0), info["yaw"] + p.get("yaw", 0), info["roll"] + p.get("roll", 0))
    d = info["exp"].clone()
    if p.get("ex") or p.get("ey"):
        d = pipe.update_delta_new_eyeball_direction(p.get("ex", 0), p.get("ey", 0), d)
    for key, fn in EDITS:
        if p.get(key):
            d = fn(p[key], d)
    x_d = p.get("zoom", 1) * info["scale"] * (info["kp"] @ R + d) + info["t"]
    if abs(p.get("shut", 0)) > 0.01:
        target = min(max(eye0 * (1 - p["shut"]), 0), 0.8)
        x_d = x_d + lp.retarget_eye(x_s, lp.calc_combined_eye_ratio([[target]], lmk))
    if abs(p.get("mouth", 0)) > 0.01:
        target = min(max(lips0 * (1 - p["mouth"]), 0), 0.8)
        x_d = x_d + lp.retarget_lip(x_s, lp.calc_combined_lip_ratio([[target]], lmk))
    x_d = lp.stitching(x_s, x_d)
    face = lp.parse_output(lp.warp_decode(feat, x_s, x_d)["out"])[0]
    whole = paste_back(face, M, img, mask)
    return cv2.resize(whole[sy : sy + side, sx : sx + side], (SIZE, SIZE), interpolation=cv2.INTER_AREA)


# Frames are kept per clip, under a hash of the photo, and re-rendered only
# when the clip's parameters change
work = BUILD / f"{args.theme}-frames" / hashlib.sha1(photo.read_bytes()).hexdigest()[:12]
total = sum(len(seq) for _, seq in CLIPS)
done, t0 = 0, time.time()
for name, seq in CLIPS:
    d = work / name
    sig = hashlib.sha1(json.dumps([sorted(p.items()) for p in seq] + [QUALITY]).encode()).hexdigest()
    if (d / "sig").exists() and (d / "sig").read_text() == sig:
        done += len(seq)
        continue
    d.mkdir(parents=True, exist_ok=True)
    t1 = time.time()
    for i, p in enumerate(seq):
        cv2.imwrite(str(d / f"{i:03d}.jpg"), cv2.cvtColor(render(p), cv2.COLOR_RGB2BGR), [cv2.IMWRITE_JPEG_QUALITY, QUALITY])
    (d / "sig").write_text(sig)
    done += len(seq)
    print(f"{name}: {len(seq)} frames in {time.time() - t1:.1f}s, {done}/{total}", flush=True)

# ---------- Packing ----------

# The theme's look, if it has one, goes into every frame here, so the
# rendered frames stay as they are and the look can change without them
keep = None
if hasattr(F, "LOOK"):
    L = F.LOOK

    def smooth(x):
        x = np.clip(x, 0, 1)
        return x * x * (3 - 2 * x)

    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32) + 0.5
    r = np.hypot(xx - SIZE / 2, yy - SIZE / 2)
    keep = (1 - L["melt"] * smooth((r - L["melt_from"]) / (SIZE / 2 - L["melt_from"])))
    keep *= 1 - L["scrim"] * smooth((yy - L["scrim_from"]) / (SIZE - L["scrim_from"]))
    keep = keep[..., None]


def packed(path):
    if keep is None:
        return path.read_bytes()
    im = cv2.imread(str(path)).astype(np.float32) * keep
    return cv2.imencode(".jpg", np.clip(im + 0.5, 0, 255).astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, QUALITY + 5])[1].tobytes()


# ---------- Text ----------

ASCII = {chr(c) for c in range(0x20, 0x7F)}


def theme_chars():
    """Every character outside ASCII in the theme's C++ strings: what its lines can say"""
    import re

    chars = set()
    for src in (DEVICES / "themes" / args.theme).glob("*.cpp"):
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src.read_text()):
            chars.update(c for c in lit if ord(c) > 127)
    return chars


def gb2312():
    chars = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


def find_face(family, style, size):
    from PIL import ImageFont

    dirs = [Path("/System/Library/Fonts"), Path("/Library/Fonts"), Path.home() / "Library/Fonts"]
    dirs += sorted(Path("/System/Library/AssetsV2").glob("com_apple_MobileAsset_Font*/*/AssetData"))
    for d in dirs:
        for f in sorted(d.glob("*.tt[cf]")) + sorted(d.glob("*.otf")):
            for i in range(64):
                try:
                    font = ImageFont.truetype(str(f), size, index=i)
                except Exception:
                    break
                if font.getname() == (family, style):
                    return font
    sys.exit(f"no font {family} {style} on this computer; name another in faces.py's TEXT")


def bake(font, chars, shadow):
    """Each glyph's tight box of coverage, and with a shadow, of the shadow's"""
    from PIL import Image, ImageDraw

    asc, desc = font.getmetrics()
    pad = shadow * 2 + 2 if shadow else 1
    out = []
    for ch in sorted(chars, key=ord):
        im = Image.new("L", (font.size * 2 + 2 * pad, asc + desc + 2 * pad), 0)
        ImageDraw.Draw(im).text((pad, pad + asc), ch, font=font, fill=255, anchor="ls")
        layers = [np.array(im, np.float32)]
        if shadow:
            # Dropped a pixel and blurred: soft under the letters, not round them
            layers.append(cv2.GaussianBlur(np.roll(layers[0], 1, axis=0), (0, 0), shadow / 2) * 0.9)
        ys, xs = np.nonzero(np.maximum.reduce(layers) > 2)
        x0, x1, y0, y1 = (xs.min(), xs.max() + 1, ys.min(), ys.max() + 1) if len(xs) else (pad, pad, pad, pad)
        bits = b"".join(np.clip(l[y0:y1, x0:x1] + 0.5, 0, 255).astype(np.uint8).tobytes() for l in layers)
        out.append((ord(ch), x1 - x0, y1 - y0, x0 - pad, y0 - pad - asc, round(font.getlength(ch)), len(layers), bits))
    return asc, desc, out


def text_section():
    if not hasattr(F, "TEXT"):
        return b""
    wanted = {"line": ASCII | theme_chars(), "sub": ASCII | gb2312() | {"…", "·"}}
    faces = []
    for key, t in F.TEXT.items():
        font = find_face(*t["face"], t["size"])
        faces.append((t["size"], t.get("shadow", 0)) + bake(font, wanted.get(key, ASCII | theme_chars()), t.get("shadow", 0)))
    at = 8 + 16 * len(faces)
    tables, bitmaps, records = [], bytearray(), b""
    bits_at = at + sum(16 * len(f[4]) for f in faces)
    for size, shadow, asc, desc, glyphs in faces:
        table = b""
        for cp, w, h, x, y, adv, layers, bits in glyphs:
            table += struct.pack("<IIBBbbBBH", cp, bits_at + len(bitmaps), w, h, x, y, adv, layers, 0)
            bitmaps += bits
        records += struct.pack("<HhhHII", size, asc, -desc, shadow, len(glyphs), at)
        tables.append(table)
        at += len(table)
    sect = b"FNT1" + struct.pack("<HH", len(faces), 0) + records + b"".join(tables) + bytes(bitmaps)
    print(f"text: {', '.join(f'{len(f[4])} glyphs at {f[0]}px' for f in faces)}, {len(sect) / 1e6:.2f} MB")
    return sect


clips, jpegs = [], []
for name, seq in CLIPS:
    clips.append((name, len(jpegs), len(seq)))
    jpegs += [packed(work / name / f"{i:03d}.jpg") for i in range(len(seq))]
text = text_section()
head = struct.pack("<4sIHHHHII", b"FPK2", 0, SIZE, SIZE, F.FPS, len(clips), 0, len(text))
head += b"".join(struct.pack("<16sHH", n.encode(), first, count) for n, first, count in clips)
head += struct.pack("<I", len(jpegs))
at = len(head) + 8 * len(jpegs)
index, body = bytearray(), bytearray()
for j in jpegs:
    pad = (-at) % 4
    body += b"\0" * pad + j
    at += pad
    index += struct.pack("<II", at, len(j))
    at += len(j)
data = bytearray(head) + index + body
data += b"\0" * ((-len(data)) % 4)
if text:
    struct.pack_into("<I", data, 16, len(data))
    data += text
struct.pack_into("<I", data, 4, len(data))
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data)
os.chdir(here)
print(f"{out}: {len(clips)} clips, {len(jpegs)} frames, {len(data) / 1e6:.1f} MB, in {time.time() - t0:.0f}s")

# What each named pack is, beside it, and every pack there listed in
# index.json for the review page
if not args.out:
    (packs / f"{who}.json").write_text(json.dumps(dict(name=who, photo=photo.name, frames=work.name), ensure_ascii=False))
    index = []
    for p in sorted(packs.glob("*.pack")):
        meta = packs / f"{p.stem}.json"
        about = json.loads(meta.read_text()) if meta.exists() else dict(name=p.stem, photo="")
        index.append(dict(about, size=p.stat().st_size))
    (packs / "index.json").write_text(json.dumps(index, ensure_ascii=False, indent=1))
    print(f"{len(index)} packs in {packs}: {', '.join(x['name'] for x in index)}")
