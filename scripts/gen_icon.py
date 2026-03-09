#!/usr/bin/env python3
"""Generate FreeDOS app icon with Blinky the Fish in all App Store sizes.

Blinky the Fish mascot by Bas Snabilie, CC-BY 2.5.
SVG source: commons.wikimedia.org/wiki/File:FreeDOS_logo4_2010.svg
"""

import os
import subprocess
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Pillow not installed. Run: pip3 install Pillow")
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "..", "DOSEmu", "Assets.xcassets", "AppIcon.appiconset")
SVG_PATH = os.path.join(SCRIPT_DIR, "blinky.svg")

# All sizes needed for iOS/macOS App Store
SIZES = [
    ("icon_1024.png", 1024),
    ("icon_180.png", 180),
    ("icon_120.png", 120),
    ("icon_167.png", 167),
    ("icon_152.png", 152),
    ("icon_76.png", 76),
    ("icon_80.png", 80),
    ("icon_87.png", 87),
    ("icon_58.png", 58),
    ("icon_40.png", 40),
    ("icon_60.png", 60),
    ("icon_20.png", 20),
    ("icon_29.png", 29),
    ("icon_1024_mac.png", 1024),
    ("icon_512.png", 512),
    ("icon_256.png", 256),
    ("icon_128.png", 128),
    ("icon_64.png", 64),
    ("icon_32.png", 32),
    ("icon_16.png", 16),
]


def load_font(size):
    for font_path in [
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Courier.dfont",
        "/Library/Fonts/Courier New.ttf",
        "/System/Library/Fonts/Monaco.dfont",
    ]:
        if os.path.exists(font_path):
            return ImageFont.truetype(font_path, size)
    return ImageFont.load_default()


def render_blinky_svg(size):
    """Try to render the Blinky SVG using rsvg-convert or sips."""
    tmp_png = f"/tmp/blinky_{size}.png"

    # Extract just the fish group from the SVG for the icon
    # The fish is the last <g> element in the SVG
    # For simplicity, render the whole SVG and crop to the fish area

    # Try rsvg-convert first (from librsvg, brew install librsvg)
    try:
        subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size),
             "--keep-aspect-ratio", "-o", tmp_png, SVG_PATH],
            check=True, capture_output=True
        )
        img = Image.open(tmp_png)
        return img
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass

    # Try cairosvg (pip3 install cairosvg)
    try:
        import cairosvg
        cairosvg.svg2png(url=SVG_PATH, write_to=tmp_png,
                         output_width=size, output_height=size)
        img = Image.open(tmp_png)
        return img
    except (ImportError, Exception):
        pass

    return None


def render_icon(size):
    """Render a FreeDOS icon at the given pixel size."""
    S = 1024
    img = Image.new("RGB", (S, S), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Colors
    LIGHT_GRAY = (170, 170, 170)
    WHITE = (255, 255, 255)
    DIM = (60, 60, 80)
    SCREEN_BG = (4, 4, 18)
    BEZEL = (50, 50, 58)
    BEZEL_HIGHLIGHT = (70, 70, 78)

    # Bezel / monitor frame
    margin = int(S * 0.05)
    bezel_w = int(S * 0.03)
    radius = int(S * 0.10)

    # Outer bezel fill
    draw.rounded_rectangle(
        [margin, margin, S - margin, S - margin],
        radius=radius,
        fill=BEZEL
    )
    # Inner highlight
    draw.rounded_rectangle(
        [margin + 2, margin + 2, S - margin - 2, S - margin - 2],
        radius=radius - 1,
        fill=BEZEL_HIGHLIGHT,
    )
    # Screen inset
    screen_m = margin + bezel_w
    draw.rounded_rectangle(
        [screen_m, screen_m, S - screen_m, S - screen_m],
        radius=int(radius * 0.7),
        fill=SCREEN_BG
    )

    # Text area bounds
    pad = int(S * 0.11)
    tx = pad

    # Fonts
    header_font = load_font(int(S * 0.058))
    prompt_font = load_font(int(S * 0.11))

    # Header lines (dim, like previous output)
    y = pad - int(S * 0.01)
    header_lines = [
        "FreeDOS kernel 2043",
        "(C) The FreeDOS",
        "    Project",
        "",
        "C:\\>VER",
        "",
        "FreeDOS version 1.4",
    ]
    line_h = int(S * 0.058 * 1.25)
    for line in header_lines:
        if line:
            draw.text((tx, y), line, fill=DIM, font=header_font)
        y += line_h

    # Main prompt "C:\>" with cursor - leave room from bottom for iOS corner mask
    prompt_y = y + int(S * 0.04)
    prompt_text = "C:\\>"
    draw.text((tx, prompt_y), prompt_text, fill=LIGHT_GRAY, font=prompt_font)

    # Position cursor after prompt
    prompt_bbox = draw.textbbox((tx, prompt_y), prompt_text, font=prompt_font)
    cursor_x = prompt_bbox[2] + int(S * 0.008)
    char_w = int(S * 0.065)
    char_h = int(S * 0.105)
    cursor_y = prompt_y + int(S * 0.015)

    draw.rectangle(
        [cursor_x, cursor_y, cursor_x + char_w, cursor_y + char_h],
        fill=WHITE
    )

    # Subtle CRT scanlines (skip the cursor region to keep it clean)
    cursor_top = cursor_y
    cursor_bot = cursor_y + char_h
    cursor_left = cursor_x
    cursor_right = cursor_x + char_w
    for ys in range(screen_m, S - screen_m, 3):
        if ys >= cursor_top and ys <= cursor_bot:
            draw.line([(screen_m, ys), (cursor_left - 1, ys)], fill=(0, 0, 0), width=1)
            draw.line([(cursor_right + 1, ys), (S - screen_m, ys)], fill=(0, 0, 0), width=1)
        else:
            draw.line([(screen_m, ys), (S - screen_m, ys)], fill=(0, 0, 0), width=1)

    # Subtle vignette on screen edges
    for i in range(20):
        opacity = int(30 * (1 - i / 20))
        r = int(radius * 0.7)
        offset = screen_m + i
        draw.rounded_rectangle(
            [offset, offset, S - offset, S - offset],
            radius=max(r - i, 1),
            fill=None,
            outline=(0, 0, 0, opacity) if opacity > 0 else None,
            width=1
        )

    # Scale down
    if size != S:
        img = img.resize((size, size), Image.LANCZOS)

    return img


def generate_contents_json():
    return """{
  "images" : [
    {
      "filename" : "icon_40.png",
      "idiom" : "iphone",
      "scale" : "2x",
      "size" : "20x20"
    },
    {
      "filename" : "icon_60.png",
      "idiom" : "iphone",
      "scale" : "3x",
      "size" : "20x20"
    },
    {
      "filename" : "icon_58.png",
      "idiom" : "iphone",
      "scale" : "2x",
      "size" : "29x29"
    },
    {
      "filename" : "icon_87.png",
      "idiom" : "iphone",
      "scale" : "3x",
      "size" : "29x29"
    },
    {
      "filename" : "icon_80.png",
      "idiom" : "iphone",
      "scale" : "2x",
      "size" : "40x40"
    },
    {
      "filename" : "icon_120.png",
      "idiom" : "iphone",
      "scale" : "3x",
      "size" : "40x40"
    },
    {
      "filename" : "icon_120.png",
      "idiom" : "iphone",
      "scale" : "2x",
      "size" : "60x60"
    },
    {
      "filename" : "icon_180.png",
      "idiom" : "iphone",
      "scale" : "3x",
      "size" : "60x60"
    },
    {
      "filename" : "icon_20.png",
      "idiom" : "ipad",
      "scale" : "1x",
      "size" : "20x20"
    },
    {
      "filename" : "icon_40.png",
      "idiom" : "ipad",
      "scale" : "2x",
      "size" : "20x20"
    },
    {
      "filename" : "icon_29.png",
      "idiom" : "ipad",
      "scale" : "1x",
      "size" : "29x29"
    },
    {
      "filename" : "icon_58.png",
      "idiom" : "ipad",
      "scale" : "2x",
      "size" : "29x29"
    },
    {
      "filename" : "icon_40.png",
      "idiom" : "ipad",
      "scale" : "1x",
      "size" : "40x40"
    },
    {
      "filename" : "icon_80.png",
      "idiom" : "ipad",
      "scale" : "2x",
      "size" : "40x40"
    },
    {
      "filename" : "icon_76.png",
      "idiom" : "ipad",
      "scale" : "1x",
      "size" : "76x76"
    },
    {
      "filename" : "icon_152.png",
      "idiom" : "ipad",
      "scale" : "2x",
      "size" : "76x76"
    },
    {
      "filename" : "icon_167.png",
      "idiom" : "ipad",
      "scale" : "2x",
      "size" : "83.5x83.5"
    },
    {
      "filename" : "icon_1024.png",
      "idiom" : "ios-marketing",
      "scale" : "1x",
      "size" : "1024x1024"
    },
    {
      "filename" : "icon_16.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "16x16"
    },
    {
      "filename" : "icon_32.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "16x16"
    },
    {
      "filename" : "icon_32.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "32x32"
    },
    {
      "filename" : "icon_64.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "32x32"
    },
    {
      "filename" : "icon_128.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "128x128"
    },
    {
      "filename" : "icon_256.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "128x128"
    },
    {
      "filename" : "icon_256.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "256x256"
    },
    {
      "filename" : "icon_512.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "256x256"
    },
    {
      "filename" : "icon_512.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "512x512"
    },
    {
      "filename" : "icon_1024_mac.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "512x512"
    }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}"""


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    unique_sizes = sorted(set(s for _, s in SIZES))
    rendered = {}
    for sz in unique_sizes:
        print(f"  Rendering {sz}x{sz}...")
        rendered[sz] = render_icon(sz)

    for filename, sz in SIZES:
        path = os.path.join(OUTPUT_DIR, filename)
        rendered[sz].save(path, "PNG")
        print(f"  Saved {filename} ({sz}x{sz})")

    contents_path = os.path.join(OUTPUT_DIR, "Contents.json")
    with open(contents_path, "w") as f:
        f.write(generate_contents_json())
    print(f"  Saved Contents.json")

    assets_dir = os.path.dirname(OUTPUT_DIR)
    top_contents = os.path.join(assets_dir, "Contents.json")
    with open(top_contents, "w") as f:
        f.write('{\n  "info" : {\n    "author" : "xcode",\n    "version" : 1\n  }\n}\n')
    print(f"  Saved Assets.xcassets/Contents.json")

    print(f"\nDone! Generated {len(SIZES)} icon files.")
    print("Blinky the Fish mascot by Bas Snabilie, CC-BY 2.5")


if __name__ == "__main__":
    main()
