"""Her expressions, for devices/tools/face_pack.py: every clip the theme plays,
as LivePortrait's parameters at each moment of it. ui.cpp asks for the clips
by these names.

The parameters, all 0 at rest (the photo as it is):
    ex, ey              gaze, about -30..30: + to the screen's right, + up
    yaw, pitch, roll    the head, in degrees: yaw + turns her face to the
                        screen's right, pitch + looks down, roll + tilts it
    shut                the eyes: 1 closed, 0 as in the photo, below 0 wider
    mouth               the lips: 0 as in the photo, 1 closed, below 0 open,
                        the same on any face, shut in its photo or not
    smile, wink         about -0.3..1.3, and 0..39
    eyebrow             + raises them, - knits them
    lip0, lip1, lip3    + skews the mouth to one side, about -0.09..0.09 (a
                        step past that breaks the face); + purses the lips (a
                        pout, or with the lips closed a kiss); - presses them
                        together
    zoom                above 1 leans in, below 1 pulls back

Each state becomes three clips: <name>.in from rest to its peak, <name>.loop
at its peak with breathing, a blink and its own small motion, and <name>.out
back to rest. Reactions play once, from rest to their peak and back.
"""

import math

FPS = 12
# Baked into every frame when packing, smoothly and at full precision (on the
# display, a blend in 32 steps would show as stripes): the bottom darkening
# under her words from scrim_from px down, enough to read them by; melt would
# fade her edge into black from melt_from px out, and is off so she fills the
# round
LOOK = dict(melt_from=168, melt=0, scrim_from=290, scrim=0.5)
# Her words, drawn smooth from glyphs baked into the pack: her lines large
# with a soft shadow (only the characters in the theme's own strings), the
# line under them small (Latin and all of GB2312, for any title or command;
# too many to store a shadow for, so the display drops a crisp one of its own).
# A face is a family and style found among the fonts on this computer.
TEXT = dict(
    line=dict(face=("Noto Sans SC", "SemiBold"), size=27, shadow=4),
    sub=dict(face=("Noto Sans SC", "Medium"), size=14, shadow=0),
)
IN_S, LOOP_S, OUT_S = 0.42, 2.0, 0.42
REACT_S = 4.0
IDLE_S = 3.5


def wave(t, period=LOOP_S, phase=0.0):
    return math.sin(2 * math.pi * (t / period + phase))


def ramp(t, a, b):
    if t <= a:
        return 0.0
    if t >= b:
        return 1.0
    x = (t - a) / (b - a)
    return x * x * (3 - 2 * x)


def hold(t, a, b, c, d):
    """Up from a to b, held, down from c to d"""
    return ramp(t, a, b) * (1 - ramp(t, c, d))


def blink(t, at, dur=0.3):
    x = (t - at) / dur
    return 0.0 if x < 0 or x > 1 else math.sin(math.pi * x) ** 0.6


def idle(t):
    """At rest: breathing, a look about, two blinks"""
    away = hold(t, 1.1, 1.4, 2.0, 2.3)
    return dict(
        yaw=3 * math.sin(2 * math.pi * t / IDLE_S),
        roll=1.5 * math.sin(2 * math.pi * t / IDLE_S + 1),
        pitch=1.2 * math.sin(2 * math.pi * t / (IDLE_S / 2)),
        ex=11 * away,
        ey=3 * away,
        shut=max(blink(t, 0.5), blink(t, 2.75)),
    )


# Where each state she rests in peaks
STATES = {
    # Working
    "think": dict(smile=0.25, ey=-16, pitch=8, roll=-3),
    "read": dict(ex=-10, ey=-12, pitch=4, smile=-0.15, shut=0.15),
    "write": dict(ey=-18, pitch=7, smile=-0.1, lip1=3),
    "compact": dict(ey=30, roll=-5, smile=-0.2),
    # Waiting for you, and the nagging as it goes on
    "plead": dict(mouth=0.85, smile=-0.3, lip1=10, eyebrow=16, shut=-0.2, pitch=5, ey=6, zoom=1.04),
    "ask": dict(mouth=1, smile=0.3, eyebrow=14, shut=-0.3, pitch=-3, zoom=1.04),
    "huh": dict(mouth=1, smile=-0.3, eyebrow=-8, roll=-12, lip1=6),
    "side_eye": dict(ex=-24, yaw=7, eyebrow=-5, smile=-0.2, lip1=3, shut=0.4),
    "sulk": dict(shut=0.9, yaw=-14, pitch=6, smile=-0.35, lip1=5),
    "cold": dict(yaw=6, ex=-24, shut=0.4, smile=-0.25),
    "hmph": dict(yaw=12, pitch=-7, shut=0.9, smile=-0.25),
    # A turn's end
    "praise_me": dict(smile=0.7, roll=-7, pitch=-3),
    "unpraised": dict(mouth=0.62, smile=-0.3, lip1=10, yaw=-12, ex=-10, eyebrow=6, shut=0.25),
    "sad": dict(eyebrow=-14, ey=-10, pitch=6, smile=-0.35, shut=0.3),
    "refuse": dict(yaw=-16, shut=0.9, smile=-0.35, pitch=-4),
    "interrupt": dict(mouth=-1.3, smile=-0.3, lip1=6, eyebrow=-10, shut=-0.15, zoom=1.05),
    # Quiet, and the link
    "bored": dict(ex=14, ey=10, shut=0.3, smile=-0.2, roll=-5),
    "sleep": dict(shut=0.95, pitch=10, roll=8, smile=-0.1),
    "shocked": dict(mouth=1, lip1=12, smile=-0.35, eyebrow=-6, shut=-0.2, pitch=5),
}

# Each state's own motion in its loop, added to its peak; back where it
# started at LOOP_S, so the loop has no seam
MOTION = {
    "think": lambda t: dict(ex=3 * wave(t), ey=2 * wave(t, phase=0.25)),
    "read": lambda t: dict(ex=9 * wave(t)),
    "write": lambda t: dict(pitch=1.5 * wave(t, LOOP_S / 2)),
    "compact": lambda t: dict(ex=8 * wave(t), roll=2 * wave(t)),
    "plead": lambda t: dict(pitch=1.5 * wave(t, LOOP_S / 2)),
    "ask": lambda t: dict(roll=1.5 * wave(t)),
    "huh": lambda t: dict(roll=-1.5 * wave(t)),
    # A quick look back at you, to see if you are looking
    "side_eye": lambda t: dict(ex=24 * hold(t, 0.7, 0.85, 1.15, 1.3), yaw=-4 * hold(t, 0.7, 0.85, 1.15, 1.3)),
    "hmph": lambda t: dict(ex=10 * hold(t, 1.0, 1.15, 1.5, 1.65), shut=-0.6 * hold(t, 1.0, 1.15, 1.5, 1.65)),
    "praise_me": lambda t: dict(wink=26 * hold(t, 0.5, 0.75, 1.3, 1.55)),
    "refuse": lambda t: dict(yaw=7 * wave(t, LOOP_S / 3) * hold(t, 0.1, 0.3, 1.2, 1.5)),
    "bored": lambda t: dict(ex=4 * wave(t)),
    "sleep": lambda t: dict(pitch=1.5 * wave(t)),
    # A small shake of the head, and a blink more than usual: she can't believe it
    "shocked": lambda t: dict(yaw=3 * wave(t), shut=1.2 * blink(t, 1.05, 0.22)),
}

# One-off reactions, from rest to their peak and back. Each moves all the
# while it is up: a held face reads as a stuck picture
REACTIONS = {
    "pat": dict(shut=0.85, smile=0.6, roll=7, pitch=5),
    "poke": dict(shut=0.6, yaw=10, roll=-6, smile=0.35),
    "left_on_read": dict(eyebrow=-16, shut=0.25, lip1=6, smile=-0.35, zoom=1.05),
    "makeup": dict(smile=0.5, shut=0.5, roll=-5),
    "kiss": dict(mouth=0.7, smile=-0.3, lip1=14, wink=30, zoom=1.05, roll=-6),
}

REACT_MOTION = {
    # Leans into your hand, side to side
    "pat": lambda t: dict(roll=5 * wave(t, 1.8), pitch=1.5 * wave(t, 0.9)),
    # Flinches, then peeks at who did it
    "poke": lambda t: dict(
        shut=-0.75 * hold(t, 1.3, 1.6, 2.8, 3.1),
        yaw=-6 * hold(t, 1.3, 1.6, 2.8, 3.1),
        smile=0.2 * hold(t, 1.3, 1.6, 2.8, 3.1),
    ),
    # Narrows her eyes, then a slow tilt: really?
    "left_on_read": lambda t: dict(roll=-7 * ramp(t, 1.0, 2.0), ex=5 * wave(t, 2.0)),
    # Means to stay cross, and the smile wins
    "makeup": lambda t: dict(smile=0.35 * ramp(t, 1.2, 2.2), shut=-0.4 * hold(t, 1.0, 1.4, 2.4, 2.8), pitch=2 * wave(t, 1.0)),
    # Puckers up, and the wink comes in the middle of it
    "kiss": lambda t: dict(wink=-30 * (1 - hold(t, 0.9, 1.2, 2.4, 2.7)), roll=3 * wave(t, 2.0)),
}
