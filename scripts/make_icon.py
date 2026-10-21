from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024
CENTER = SIZE // 2
RADIUS = 496
RING_WIDTH = 32
N_LEFT = 336
N_RIGHT = 688
N_TOP = 336
N_BOTTOM = 688
N_WIDTH = 56

ROOT = Path(__file__).resolve().parent.parent
ICON_PATH = ROOT / "resources" / "icons" / "neonboard.ico"
PREVIEW_DIR = ROOT / "build"


def gradient() -> Image.Image:
    strip = Image.new("RGBA", (3, 1))
    strip.putpixel((0, 0), (34, 211, 238, 255))
    strip.putpixel((1, 0), (167, 139, 250, 255))
    strip.putpixel((2, 0), (244, 114, 182, 255))
    return strip.resize((SIZE, SIZE), Image.Resampling.BILINEAR)


def main() -> None:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    box = (CENTER - RADIUS, CENTER - RADIUS, CENTER + RADIUS, CENTER + RADIUS)
    draw.ellipse(box, fill=(5, 6, 10, 255))

    neon = gradient()

    ring_mask = Image.new("L", (SIZE, SIZE), 0)
    ring_draw = ImageDraw.Draw(ring_mask)
    ring_draw.ellipse(box, outline=255, width=RING_WIDTH)

    ring = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ring.paste(neon, (0, 0), ring_mask)
    image.alpha_composite(ring)

    n_mask = Image.new("L", (SIZE, SIZE), 0)
    n_draw = ImageDraw.Draw(n_mask)
    points = [(N_LEFT, N_BOTTOM), (N_LEFT, N_TOP), (N_RIGHT, N_BOTTOM), (N_RIGHT, N_TOP)]
    n_draw.line(points, fill=255, width=N_WIDTH, joint="curve")
    cap = N_WIDTH // 2
    for x, y in points:
        n_draw.ellipse((x - cap, y - cap, x + cap, y + cap), fill=255)

    glow_mask = n_mask.filter(ImageFilter.GaussianBlur(18))
    glow_mask = glow_mask.point(lambda value: int(value * 0.45))
    glow = Image.new("RGBA", (SIZE, SIZE), (34, 211, 238, 0))
    glow.putalpha(glow_mask)
    image.alpha_composite(glow)

    n_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    n_layer.paste(neon, (0, 0), n_mask)
    image.alpha_composite(n_layer)

    image.save(ICON_PATH, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    image.resize((256, 256), Image.Resampling.LANCZOS).save(PREVIEW_DIR / "icon_preview_256.png")
    image.resize((32, 32), Image.Resampling.LANCZOS).save(PREVIEW_DIR / "icon_preview_32.png")

    for name in ("icon_preview_256.png", "icon_preview_32.png"):
        preview = Image.open(PREVIEW_DIR / name).convert("RGBA")
        width, height = preview.size
        corners = [preview.getpixel((0, 0)), preview.getpixel((width - 1, 0)),
                   preview.getpixel((0, height - 1)), preview.getpixel((width - 1, height - 1))]
        center = preview.getpixel((width // 2, height // 2))
        print(name, "corners", corners, "center", center)


if __name__ == "__main__":
    main()
