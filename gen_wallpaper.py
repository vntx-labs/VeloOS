#!/usr/bin/env python3
"""
gen_wallpaper.py - Generiert das native VeloOS Wallpaper mit Header (WALL.BIN)
und WALL.PNG in perfekter 1920x1080 Qualität.
"""
import math
import struct

try:
    from PIL import Image
except ImportError:
    Image = None

WIDTH = 1920
HEIGHT = 1080

def alpha_blend_rgb(bg, fg, alpha):
    inv = 1.0 - alpha
    return (
        int(fg[0] * alpha + bg[0] * inv),
        int(fg[1] * alpha + bg[1] * inv),
        int(fg[2] * alpha + bg[2] * inv)
    )

def generate_wallpaper():
    print(f"[+] Generiere VeloOS 1080p Wallpaper mit Vektor-Logo ({WIDTH}x{HEIGHT})...")
    
    img_data = [[(0, 0, 0) for _ in range(WIDTH)] for _ in range(HEIGHT)]

    # 1. Linearer Hintergrund-Farbverlauf (0x00060C1A -> 0x000E2244)
    top_col = (26, 12, 6)     # B, G, R
    bot_col = (68, 34, 14)    # B, G, R

    for y in range(HEIGHT):
        ny = y / float(HEIGHT)
        cr = int(top_col[2] + (bot_col[2] - top_col[2]) * ny)
        cg = int(top_col[1] + (bot_col[1] - top_col[1]) * ny)
        cb = int(top_col[0] + (bot_col[0] - top_col[0]) * ny)
        for x in range(WIDTH):
            img_data[y][x] = (cb, cg, cr)

    # 2. Vektor-Logo Koordinaten
    cx = WIDTH // 2
    cy = HEIGHT // 2 - 20

    def draw_circle_aa(radius, thickness, color_bgr):
        r_in = radius - thickness / 2.0
        r_out = radius + thickness / 2.0
        margin = int(r_out + 3)
        for y in range(max(0, cy - margin), min(HEIGHT, cy + margin + 1)):
            for x in range(max(0, cx - margin), min(WIDTH, cx + margin + 1)):
                d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
                cov = 0.0
                if r_in - 0.5 <= d <= r_out + 0.5:
                    c_in = d - (r_in - 0.5)
                    c_out = (r_out + 0.5) - d
                    cov = max(0.0, min(1.0, min(c_in, c_out)))
                if cov > 0.0:
                    img_data[y][x] = alpha_blend_rgb(img_data[y][x], color_bgr, cov)

    def draw_line_aa(x1, y1, x2, y2, thickness, color_bgr):
        vx = float(x2 - x1)
        vy = float(y2 - y1)
        len_sq = vx * vx + vy * vy
        if len_sq == 0: return
        half_th = thickness / 2.0
        margin = int(half_th + 3)
        min_x = max(0, min(x1, x2) - margin)
        max_x = min(WIDTH - 1, max(x1, x2) + margin)
        min_y = max(0, min(y1, y2) - margin)
        max_y = min(HEIGHT - 1, max(y1, y2) + margin)

        for y in range(min_y, max_y + 1):
            for x in range(min_x, max_x + 1):
                dx_a = float(x - x1)
                dy_a = float(y - y1)
                t = max(0.0, min(1.0, (dx_a * vx + dy_a * vy) / len_sq))
                qx = x1 + t * vx
                qy = y1 + t * vy
                dist = math.sqrt((x - qx) ** 2 + (y - qy) ** 2)
                cov = max(0.0, min(1.0, half_th + 0.5 - dist))
                if cov > 0.0:
                    img_data[y][x] = alpha_blend_rgb(img_data[y][x], color_bgr, cov)

    # Äußerer & innerer Kreis
    draw_circle_aa(90, 4.0, (0xD6, 0x62, 0x1B))  # #001B62D6 (Blau)
    draw_circle_aa(76, 2.5, (0xF7, 0xA2, 0x7A))  # #007AA2F7 (Hellblau)

    # Äußerer Chevron 'V' (Cyan #0000FFCC -> BGR: 0xCC, 0xFF, 0x00)
    draw_line_aa(cx - 42, cy - 38, cx, cy + 42, 6.0, (0xCC, 0xFF, 0x00))
    draw_line_aa(cx, cy + 42, cx + 42, cy - 38, 6.0, (0xCC, 0xFF, 0x00))

    # Innerer Chevron 'V' (Hellblau #007AA2F7 -> BGR: 0xF7, 0xA2, 0x7A)
    draw_line_aa(cx - 24, cy - 32, cx, cy + 18, 4.0, (0xF7, 0xA2, 0x7A))
    draw_line_aa(cx, cy + 18, cx + 24, cy - 32, 4.0, (0xF7, 0xA2, 0x7A))

    # 3. Typografie
    font_bitmap = {
        'V': [0x82,0x82,0x82,0x44,0x44,0x28,0x10],
        'E': [0xFE,0x80,0x80,0xFC,0x80,0x80,0xFE],
        'L': [0x80,0x80,0x80,0x80,0x80,0x80,0xFE],
        'O': [0x7C,0x82,0x82,0x82,0x82,0x82,0x7C],
        'S': [0x7C,0x82,0x80,0x7C,0x02,0x82,0x7C],
        '6': [0x7C,0x80,0x80,0xFC,0x82,0x82,0x7C],
        '4': [0x18,0x28,0x48,0x88,0xFE,0x08,0x08],
        '-': [0x00,0x00,0x00,0x7E,0x00,0x00,0x00],
        'B': [0xFC,0x82,0x82,0xFC,0x82,0x82,0xFC],
        'i': [0x10,0x00,0x10,0x10,0x10,0x10,0x10],
        't': [0x10,0x10,0x7C,0x10,0x10,0x10,0x0E],
        'M': [0x82,0xC6,0xAA,0x92,0x82,0x82,0x82],
        'a': [0x00,0x00,0x7C,0x02,0x7E,0x82,0x7E],
        'r': [0x00,0x00,0xAE,0x60,0x40,0x40,0x40],
        'e': [0x00,0x00,0x7C,0x82,0xFE,0x80,0x7C],
        'C': [0x7C,0x82,0x80,0x80,0x80,0x82,0x7C],
        'o': [0x00,0x00,0x7C,0x82,0x82,0x82,0x7C],
        ' ': [0x00,0x00,0x00,0x00,0x00,0x00,0x00]
    }

    def draw_text_clean(text, start_x, start_y, scale, color_bgr):
        cur_x = start_x
        for ch in text:
            glyph = font_bitmap.get(ch, font_bitmap[' '])
            for gy, row in enumerate(glyph):
                for gx in range(8):
                    if row & (1 << (7 - gx)):
                        for sy in range(scale):
                            for sx in range(scale):
                                px = cur_x + gx * scale + sx
                                py = start_y + gy * scale + sy
                                if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                                    img_data[py][px] = color_bgr
            cur_x += 8 * scale + 2

    draw_text_clean("V E L O   O S", cx - 95, cy + 105, 2, (0xCC, 0xFF, 0x00))
    draw_text_clean("64-Bit Bare-Metal Core", cx - 145, cy + 130, 2, (0xF7, 0xA2, 0x7A))

    # 4. Binärdatei mit 8-Byte Header schreiben [uint32 Width, uint32 Height]
    bin_file = bytearray()
    bin_file.extend(struct.pack("<II", WIDTH, HEIGHT))

    png_pixels = [] if Image else None

    for y in range(HEIGHT):
        for x in range(WIDTH):
            b, g, r = img_data[y][x]
            bin_file.extend(bytes([b & 0xFF, g & 0xFF, r & 0xFF, 0x00]))
            if png_pixels is not None:
                png_pixels.append((r, g, b))

    with open("WALL.BIN", "wb") as f:
        f.write(bin_file)
    print("[+] 'WALL.BIN' erfolgreich generiert (inkl. 8-Byte Header).")

    if png_pixels is not None:
        img = Image.new("RGB", (WIDTH, HEIGHT))
        img.putdata(png_pixels)
        img.save("WALL.PNG")
        print("[+] 'WALL.PNG' Vorschau gespeichert.")

if __name__ == "__main__":
    generate_wallpaper()