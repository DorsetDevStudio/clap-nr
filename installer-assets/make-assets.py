"""
make-assets.py  --  generate clap-nr installer image assets using Pillow.

Outputs (all in the same directory as this script):
  icon-256.png     256x256  icon (used for ICO source)
  icon-48.png       48x48
  icon-32.png       32x32
  icon-16.png       16x16
  clap-nr.ico      multi-resolution Windows icon (16/32/48/256)
  wizard-panel.png 164x314  Inno Setup WizardImageFile
  wizard-small.png  55x58   Inno Setup WizardSmallImageFile

Run from the project root:
    python installer-assets/make-assets.py
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

YELLOW = (255, 215, 0)
YELLOW_DIM = (200, 160, 0)
BLACK = (17, 17, 17)
BLACK2 = (10, 10, 10)
GREY = (120, 120, 120)

OUTDIR = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def stripe_bg(draw, w, h, stripe_w=18, gap=18, opacity=20):
    """Draw subtle 45-degree diagonal yellow stripes across the whole canvas."""
    total = w + h
    for offset in range(-h, total, stripe_w + gap):
        x0 = offset
        y0 = 0
        x1 = offset + stripe_w
        y1 = 0
        # Four corners of a diagonal stripe band
        pts = [(x0, 0), (x0 + h, h), (x1 + h, h), (x1, 0)]
        draw.polygon(pts, fill=(*YELLOW, opacity))


def noisy_waveform_points(x0, x1, cy, amplitude=30, n=14):
    """Return a polyline of a jagged noisy waveform."""
    import random
    rng = random.Random(42)  # fixed seed for reproducibility
    pts = []
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        # alternating up/down with random magnitude
        sign = 1 if i % 2 == 0 else -1
        mag = rng.uniform(0.4, 1.0) * amplitude * sign
        pts.append((x, cy + mag))
    return pts


def sine_wave_points(x0, x1, cy, amplitude=28, periods=1.0, n=80):
    """Return a polyline approximating a clean sine wave."""
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + (x1 - x0) * t
        y = cy - amplitude * math.sin(t * periods * 2 * math.pi)
        pts.append((x, y))
    return pts


def draw_polyline(draw, pts, color, width):
    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=color, width=width)


def rounded_rect(draw, xy, radius, fill):
    x0, y0, x1, y1 = xy
    draw.rectangle([x0 + radius, y0, x1 - radius, y1], fill=fill)
    draw.rectangle([x0, y0 + radius, x1, y1 - radius], fill=fill)
    draw.ellipse([x0, y0, x0 + radius * 2, y0 + radius * 2], fill=fill)
    draw.ellipse([x1 - radius * 2, y0, x1, y0 + radius * 2], fill=fill)
    draw.ellipse([x0, y1 - radius * 2, x0 + radius * 2, y1], fill=fill)
    draw.ellipse([x1 - radius * 2, y1 - radius * 2, x1, y1], fill=fill)


# ---------------------------------------------------------------------------
# Icon (square, works at 16..256 px)
# ---------------------------------------------------------------------------

def make_icon(size):
    scale = size / 256
    w = h = size
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img, "RGBA")

    radius = max(2, int(22 * scale))
    rounded_rect(draw, (0, 0, w - 1, h - 1), radius, (*BLACK, 255))

    if size >= 32:
        stripe_bg(draw, w, h,
                  stripe_w=max(4, int(14 * scale)),
                  gap=max(4, int(14 * scale)),
                  opacity=18)

    # top/bottom accent bars
    bar_h = max(2, int(8 * scale))
    draw.rectangle([(0, 0), (w, bar_h)], fill=(*YELLOW, 160))
    draw.rectangle([(0, h - bar_h), (w, h)], fill=(*YELLOW, 160))

    if size >= 48:
        cy = int(h * 0.47)
        mid = w // 2

        # noisy waveform (left)
        amp = max(8, int(32 * scale))
        noise_pts = noisy_waveform_points(int(20 * scale), mid - int(10 * scale), cy, amplitude=amp)
        lw = max(1, int(4 * scale))
        draw_polyline(draw, noise_pts, YELLOW, lw)

        # centre divider
        div_top = int(60 * scale)
        div_bot = int(172 * scale)
        draw.line([(mid, div_top), (mid, div_bot)], fill=(*YELLOW, 140), width=max(1, int(2 * scale)))

        # right-pointing arrow at mid height
        aw = int(10 * scale)
        ah = int(7 * scale)
        ax = mid - int(8 * scale)
        ay = cy
        draw.polygon([(ax, ay - ah), (ax + aw, ay), (ax, ay + ah)], fill=YELLOW)

        # clean sine wave (right)
        sine_pts = sine_wave_points(mid + int(10 * scale), int(234 * scale), cy,
                                    amplitude=max(8, int(28 * scale)), periods=1.0)
        draw_polyline(draw, sine_pts, YELLOW, lw)

        # "NR" text
        font_size = max(8, int(38 * scale))
        try:
            font = ImageFont.truetype("arialbd.ttf", font_size)
        except Exception:
            font = ImageFont.load_default()
        text = "NR"
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        tx = (w - tw) // 2
        ty = int(195 * scale)
        draw.text((tx, ty), text, font=font, fill=YELLOW)

    elif size >= 16:
        # at 16/32 px just draw "NR" centred, no waveform clutter
        font_size = max(6, int(size * 0.52))
        try:
            font = ImageFont.truetype("arialbd.ttf", font_size)
        except Exception:
            font = ImageFont.load_default()
        text = "NR"
        bbox = draw.textbbox((0, 0), text, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        draw.text(((w - tw) // 2, (h - th) // 2 - int(2 * scale)),
                  text, font=font, fill=YELLOW)

    return img


# ---------------------------------------------------------------------------
# Wizard panel  164 x 314
# ---------------------------------------------------------------------------

def make_wizard_panel():
    W, H = 164, 314
    img = Image.new("RGB", (W, H), BLACK2)
    draw = ImageDraw.Draw(img, "RGBA")

    # bumblebee stripe band at top
    band_h = 68
    draw.rectangle([(0, 0), (W, band_h)], fill=(22, 16, 0))
    stripe_w = 16
    gap = 16
    for ox in range(-band_h, W + band_h, stripe_w + gap):
        pts = [(ox, 0), (ox + band_h, band_h),
               (ox + band_h + stripe_w, band_h), (ox + stripe_w, 0)]
        draw.polygon(pts, fill=(*YELLOW, 120))
    # darken overlay
    draw.rectangle([(0, 0), (W, band_h)], fill=(13, 13, 13, 100))

    # title text
    try:
        font_title = ImageFont.truetype("arialbd.ttf", 22)
        font_sub   = ImageFont.truetype("arial.ttf", 9)
        font_label = ImageFont.truetype("arial.ttf", 8)
        font_byline = ImageFont.truetype("arial.ttf", 8)
    except Exception:
        font_title = font_sub = font_label = font_byline = ImageFont.load_default()

    title = "clap-nr"
    tb = draw.textbbox((0, 0), title, font=font_title)
    draw.text(((W - (tb[2] - tb[0])) // 2, 18), title, font=font_title, fill=YELLOW)

    sub = "NOISE REDUCTION"
    sb = draw.textbbox((0, 0), sub, font=font_sub)
    draw.text(((W - (sb[2] - sb[0])) // 2, 45), sub, font=font_sub, fill=YELLOW_DIM)

    # separator
    draw.line([(18, 76), (W - 18, 76)], fill=(*YELLOW, 100), width=1)

    # subtle stripe texture on main area
    stripe_bg(draw, W, H - band_h - 44, stripe_w=10, gap=10, opacity=12)

    # BEFORE label
    bl = "BEFORE"
    bb = draw.textbbox((0, 0), bl, font=font_label)
    draw.text(((W - (bb[2] - bb[0])) // 2, 88), bl, font=font_label, fill=GREY)

    # noisy waveform
    cy_noise = 116
    noise_pts = noisy_waveform_points(10, W - 10, cy_noise, amplitude=20, n=14)
    draw_polyline(draw, noise_pts, YELLOW, 2)

    # downward arrow
    ax = W // 2
    draw.line([(ax, 144), (ax, 170)], fill=(*YELLOW, 130), width=2)
    draw.polygon([(ax - 7, 164), (ax, 178), (ax + 7, 164)], fill=(*YELLOW, 130))

    # AFTER label
    al = "AFTER"
    ab = draw.textbbox((0, 0), al, font=font_label)
    draw.text(((W - (ab[2] - ab[0])) // 2, 184), al, font=font_label, fill=GREY)

    # clean sine wave
    cy_sine = 210
    sine_pts = sine_wave_points(10, W - 10, cy_sine, amplitude=20, periods=1.5)
    draw_polyline(draw, sine_pts, YELLOW, 2)

    # bumblebee stripe band at bottom
    bot_y = H - 44
    draw.rectangle([(0, bot_y), (W, H)], fill=(22, 16, 0))
    for ox in range(-44, W + 44, stripe_w + gap):
        pts = [(ox, bot_y), (ox + 44, H),
               (ox + 44 + stripe_w, H), (ox + stripe_w, bot_y)]
        draw.polygon(pts, fill=(*YELLOW, 100))
    draw.rectangle([(0, bot_y), (W, H)], fill=(13, 13, 13, 100))

    byline = "Station Master Group Ltd"
    byb = draw.textbbox((0, 0), byline, font=font_byline)
    draw.text(((W - (byb[2] - byb[0])) // 2, H - 18), byline, font=font_byline, fill=GREY)

    return img


# ---------------------------------------------------------------------------
# Wizard small header  55 x 58
# ---------------------------------------------------------------------------

def make_wizard_small():
    W, H = 55, 58
    img = Image.new("RGB", (W, H), BLACK2)
    draw = ImageDraw.Draw(img, "RGBA")

    # subtle stripe texture
    stripe_bg(draw, W, H, stripe_w=7, gap=7, opacity=20)

    # waveform (squeezed) - noisy left, clean right
    mid = W // 2
    cy = H // 2 - 4

    noise_pts = noisy_waveform_points(3, mid - 2, cy, amplitude=10, n=8)
    draw_polyline(draw, noise_pts, YELLOW, 1)

    draw.line([(mid, cy - 12), (mid, cy + 12)], fill=(*YELLOW, 130), width=1)

    sine_pts = sine_wave_points(mid + 2, W - 3, cy, amplitude=10, periods=1.0)
    draw_polyline(draw, sine_pts, YELLOW, 1)

    # "NR" text
    try:
        font = ImageFont.truetype("arialbd.ttf", 12)
    except Exception:
        font = ImageFont.load_default()
    text = "NR"
    tb = draw.textbbox((0, 0), text, font=font)
    draw.text(((W - (tb[2] - tb[0])) // 2, H - 18), text, font=font, fill=YELLOW)

    # top accent line
    draw.line([(0, 0), (W, 0)], fill=YELLOW, width=2)

    return img


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Icon PNGs at each size
    for size in (256, 48, 32, 16):
        icon = make_icon(size)
        path = os.path.join(OUTDIR, f"icon-{size}.png")
        icon.save(path)
        print(f"  wrote {path}")

    # Multi-resolution ICO
    ico_path = os.path.join(OUTDIR, "clap-nr.ico")
    icons = [make_icon(s).convert("RGBA") for s in (256, 48, 32, 16)]
    icons[0].save(
        ico_path,
        format="ICO",
        sizes=[(256, 256), (48, 48), (32, 32), (16, 16)],
        append_images=icons[1:],
    )
    print(f"  wrote {ico_path}")

    # Wizard panel
    panel_path = os.path.join(OUTDIR, "wizard-panel.png")
    make_wizard_panel().save(panel_path)
    print(f"  wrote {panel_path}")

    # Wizard small header
    small_path = os.path.join(OUTDIR, "wizard-small.png")
    make_wizard_small().save(small_path)
    print(f"  wrote {small_path}")

    print("\nAll assets generated successfully.")
