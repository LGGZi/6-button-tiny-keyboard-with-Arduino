#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
虚拟 WS2812B 环境 —— 在 PC 上跑固件里同一套灯效数学，用来检查渐变是否连续。

为什么需要它：
  * 固件里 `wsMix` / `wsDim` / 时间步进都是整数运算，肉眼很难判断"中间有没有跳色"；
  * 在板子上调一次要烧录、拔插、看半天；这里直接算每一帧的 RGB，输出成图片和 CSV，
    并且自动统计"相邻帧的最大通道变化"，超过阈值就报警。

它**不是** FastLED 的模拟器：只复刻 `WS2812BManager.h` 里我们自己写的颜色数学
（`wsMix8` / `wsScale` / 时间步进 / 各效果的更新公式），因此换效果或改公式时要同步这里。

用法：
    python led_sim.py --effect 18 --frames 60 --fps 50 --speed 8 \
                      --color1 FE0107 --color2 00E6FF --out out

    python led_sim.py --list          # 列出效果编号

输出：
    out/frames.png    胶片图（每行一帧，6 颗灯）
    out/timeline.csv  每帧每颗灯的 RGB
    out/report.txt    连续性统计

只依赖 Python 标准库。
"""

import argparse
import csv
import math
import os
import struct
import zlib

LEDS = 6
COLS = 3
ROWS = 2

EFFECT_NAMES = {
    0: "mono/off", 1: "mono/solid", 2: "mono/press", 3: "mono/press-fade",
    4: "mono/typing-heat", 5: "mono/breath", 6: "mono/wave", 7: "mono/sparkle",
    8: "mono/ripple", 9: "mono/comet",
    10: "rainbow/flow", 11: "rainbow/breath", 12: "rainbow/wave",
    13: "rainbow/sparkle", 14: "rainbow/comet",
    15: "dual/breath", 16: "dual/press", 17: "dual/press-fade", 18: "dual/wave",
    19: "dual/comet", 20: "dual/typing", 21: "dual/gradient", 22: "dual/ripple",
}


# ---------------------------------------------------------------- 固件同款数学
def qadd8(a, b):
    return min(255, a + b)


def qsub8(a, b):
    return max(0, a - b)


def scale8(v, level):
    """对应固件的 wsScale：level=255 原样返回，其余按 level/256 乘，能到 0。"""
    if level >= 255:
        return v
    return (v * level) >> 8


def dim(c, level):
    return tuple(scale8(x, level) for x in c)


def mix8(a, b, t):
    """对应固件的 wsMix8：端点精确的线性插值。"""
    if t <= 0:
        return a
    if t >= 255:
        return b
    inv = 255 - t
    return (a * inv + b * t + 127) // 255


def mix(c1, c2, t):
    return tuple(mix8(a, b, t) for a, b in zip(c1, c2))


def sin8(x):
    """近似 FastLED sin8：0..255。"""
    x &= 0xFF
    return int(round(127.5 + 127.5 * math.sin(2 * math.pi * x / 256.0)))


def key_col(k):
    return k % COLS


def key_row(k):
    return k // COLS


def key_dist(a, b):
    return abs(key_col(a) - key_col(b)) + abs(key_row(a) - key_row(b))


def parse_color(text):
    text = text.lstrip("#")
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


# ---------------------------------------------------------------- 效果模拟
class State:
    def __init__(self, effect, speed, fps, c1, c2):
        self.effect = effect
        self.speed = speed
        self.fps = fps
        self.c1 = c1
        self.c2 = c2
        self.leds = [(0, 0, 0)] * LEDS
        self.phase = 0
        self.hue = 0
        self.heat = [0] * LEDS
        self.glow = [0] * LEDS
        self.breath = 0
        self.comet_pos = 0
        self.comet_acc = 0.0
        self.ripple_key = -1
        self.ripple_age = 0
        self.prev_keys = 0

        frame_ms = 1000.0 / fps
        self.dt = max(8.0, min(60.0, frame_ms))

    def scale_by_dt(self, per_ref):
        v = int(per_ref * self.dt / 20.0)
        return max(1, min(255, v))

    def phase_step(self):
        return self.scale_by_dt(1 + (self.speed >> 1))

    def fade_step(self):
        v = int((12 + self.speed * 3) * self.dt / 20.0)
        return max(6, min(200, v))

    def step(self, keys):
        e = self.effect
        if e == 0:
            self.leds = [(0, 0, 0)] * LEDS
        elif e == 1:
            self.leds = [self.c1] * LEDS
        elif e == 2:
            self.leds = [self.c1 if (keys >> i) & 1 else (0, 0, 0) for i in range(LEDS)]
        elif e == 3:
            out = []
            for i in range(LEDS):
                out.append(self.c1 if (keys >> i) & 1 else dim(self.leds[i], 255 - self.fade_step()))
            self.leds = out
        elif e == 4:
            hot = self.scale_by_dt(6 + self.speed)
            cool = self.scale_by_dt(1 + (self.speed >> 1))
            for i in range(LEDS):
                if (keys >> i) & 1:
                    self.heat[i] = qadd8(self.heat[i], hot)
                else:
                    self.heat[i] = qsub8(self.heat[i], cool)
            self.leds = [dim(self.c1, h) for h in self.heat]
        elif e == 5:
            level = sin8(self.phase)
            self.phase = (self.phase + self.phase_step()) & 0xFF
            self.leds = [dim(self.c1, level)] * LEDS
        elif e == 6:
            self.leds = [dim(self.c1, sin8(self.phase + key_col(i) * 60 + key_row(i) * 20)) for i in range(LEDS)]
            self.phase = (self.phase + self.phase_step()) & 0xFF
        elif e == 7:
            fade = self.fade_step()
            self.leds = [dim(c, 255 - fade) for c in self.leds]
            if (self.phase & 0x3F) == 0:
                self.leds[(self.phase >> 3) % LEDS] = self.c1
            self.phase = (self.phase + 7) & 0xFF
        elif e in (8, 22):
            self._ripple(keys, dual=(e == 22))
        elif e in (9, 14, 19):
            self._comet(e, keys)
        elif e == 10:
            out = []
            for i in range(LEDS):
                h = (self.hue + key_col(i) * 42 + key_row(i) * 85) & 0xFF
                out.append(hsv(h, 255, 255))
            self.leds = out
            self.hue = (self.hue + self.phase_step()) & 0xFF
        elif e == 11:
            level = sin8(self.phase)
            self.leds = [hsv((self.hue + key_col(i) * 30 + key_row(i) * 60) & 0xFF, 255, level) for i in range(LEDS)]
            self.hue = (self.hue + self.phase_step()) & 0xFF
            self.phase = (self.phase + self.phase_step()) & 0xFF
        elif e == 12:
            out = []
            for i in range(LEDS):
                h = (self.hue + key_col(i) * 50 + key_row(i) * 20) & 0xFF
                v = sin8(self.phase + key_col(i) * 60 + key_row(i) * 20)
                out.append(hsv(h, 255, v))
            self.leds = out
            self.phase = (self.phase + self.phase_step()) & 0xFF
            self.hue = (self.hue + 2) & 0xFF
        elif e == 13:
            fade = self.fade_step()
            self.leds = [dim(c, 255 - fade) for c in self.leds]
            self.phase = (self.phase + 9) & 0xFF
            if (self.phase & 0x1F) < 9:
                self.leds[(self.phase >> 2) % LEDS] = hsv(self.phase, 255, 255)
        elif e == 15:
            t = self.breath * 2 if self.breath < 128 else (255 - self.breath) * 2
            self.leds = [mix(self.c1, self.c2, t)] * LEDS
            self.breath = (self.breath + self.scale_by_dt(1 + (self.speed >> 2))) & 0xFF
        elif e == 16:
            self.leds = [self.c2 if (keys >> i) & 1 else self.c1 for i in range(LEDS)]
        elif e == 17:
            fade = self.scale_by_dt(4 + self.speed)
            for i in range(LEDS):
                if (keys >> i) & 1:
                    self.glow[i] = 255
                else:
                    self.glow[i] = qsub8(self.glow[i], fade)
            self.leds = [mix(self.c1, self.c2, g) for g in self.glow]
        elif e == 18:
            self.leds = [mix(self.c1, self.c2, sin8(self.phase + key_col(i) * 85 + key_row(i) * 30)) for i in range(LEDS)]
            self.phase = (self.phase + self.scale_by_dt(2 + (self.speed >> 1))) & 0xFF
        elif e == 20:
            hot = self.scale_by_dt(6 + self.speed)
            cool = self.scale_by_dt(1 + (self.speed >> 1))
            for i in range(LEDS):
                if (keys >> i) & 1:
                    self.heat[i] = qadd8(self.heat[i], hot)
                else:
                    self.heat[i] = qsub8(self.heat[i], cool)
            self.leds = [mix(self.c2, self.c1, h) for h in self.heat]
        elif e == 21:
            self.leds = [mix(self.c1, self.c2, (key_col(i) * 255) // (COLS - 1)) for i in range(LEDS)]
        else:
            self.leds = [(0, 0, 0)] * LEDS

        self.prev_keys = keys

    def _ripple(self, keys, dual):
        if self.ripple_key < 0:
            released = self.prev_keys & ~keys
            for i in range(LEDS - 1, -1, -1):
                if (released >> i) & 1:
                    self.ripple_key = i
                    self.ripple_age = 0
                    break
        delay = 80 - self.speed * 2
        dur = 1800 - self.speed * 50
        out = []
        for i in range(LEDS):
            level = 0
            if self.ripple_key >= 0:
                age = int(self.ripple_age) - key_dist(self.ripple_key, i) * delay
                if age > 0:
                    v = 255 - (age * 255) // dur
                    if v > 0:
                        level = v
            out.append(mix(self.c1, self.c2, level) if dual else dim(self.c1, level))
            if (not dual) and ((keys >> i) & 1):
                out[i] = self.c1
        self.leds = out
        if self.ripple_key >= 0:
            self.ripple_age += self.dt
            if self.ripple_age > 1400:
                self.ripple_key = -1
                self.ripple_age = 0

    def _comet(self, e, keys):
        """与固件一致：头在环上连续移动，按 0/1/2/3 格的亮度曲线取亮度。"""
        curve = (255, 140, 50, 0)
        move = 380 - self.speed * 15

        self.comet_acc += self.dt
        while self.comet_acc >= move:
            self.comet_acc -= move
            self.comet_pos = (self.comet_pos + 1) % LEDS
        frac = int(self.comet_acc * 255 / move)

        head256 = self.comet_pos * 256 + frac
        out = []
        for i in range(LEDS):
            d = i * 256 - head256
            if d < 0:
                d = -d
            if d > LEDS * 128:
                d = LEDS * 256 - d
            if d >= 768:
                level = 0
            else:
                seg = d >> 8
                f8 = d & 0xFF
                a, bb = curve[seg], curve[seg + 1]
                level = a + int((bb - a) * f8 / 256)
            if e == 9:
                out.append(dim(self.c1, level))
            elif e == 14:
                out.append(hsv((self.hue + (d >> 5)) & 0xFF, 255, level))
            else:
                out.append(mix(self.c1, self.c2, level))

        if e == 9:
            for i in range(LEDS):
                if (keys >> i) & 1:
                    out[i] = self.c1
        self.leds = out
        self.hue = (self.hue + self.phase_step()) & 0xFF


def hsv(h, s, v):
    h = (h & 0xFF) / 255.0
    s = s / 255.0
    v = v / 255.0
    i = int(h * 6) % 6
    f = h * 6 - int(h * 6)
    p = v * (1 - s)
    q = v * (1 - f * s)
    t = v * (1 - (1 - f) * s)
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
    return (int(r * 255), int(g * 255), int(b * 255))


# ---------------------------------------------------------------- 输出
def write_png(path, width, height, rows):
    """rows: 每行 (y) 的像素列表 [(r,g,b), ...]"""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(rows[y][x])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def render(frames, path, cell=46, gap=6, frame_gap=6):
    """把每帧画成一行圆形灯珠。"""
    width = LEDS * (cell + gap) + gap
    height = len(frames) * (cell + frame_gap) + frame_gap
    rows = []
    for _ in range(height):
        rows.append([(24, 24, 28)] * width)
    for fi, frame in enumerate(frames):
        y0 = frame_gap + fi * (cell + frame_gap)
        for i, color in enumerate(frame):
            x0 = gap + i * (cell + gap)
            cx, cy, r = x0 + cell / 2.0, y0 + cell / 2.0, cell / 2.0 - 2
            for yy in range(cell):
                for xx in range(cell):
                    dx = xx + 0.5 - (cx - x0)
                    dy = yy + 0.5 - (cy - y0)
                    if dx * dx + dy * dy <= r * r:
                        px, py = x0 + xx, y0 + yy
                        if py < height and px < width:
                            rows[py][px] = color
    write_png(path, width, height, rows)


def continuity(frames, masks=None, label="all"):
    """每颗灯相邻帧的最大通道变化；masks 给出时跳过按键状态变化的那一帧。"""
    worst = 0
    where = None
    for i in range(LEDS):
        for f in range(1, len(frames)):
            if masks is not None and masks[f] != masks[f - 1]:
                continue
            a, b = frames[f - 1][i], frames[f][i]
            d = max(abs(a[c] - b[c]) for c in range(3))
            if d > worst:
                worst = d
                where = (i, f)
    return worst, where


def main():
    ap = argparse.ArgumentParser(description="virtual WS2812B strip for the 6kb firmware")
    ap.add_argument("--effect", type=int, default=18)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--fps", type=int, default=50)
    ap.add_argument("--speed", type=int, default=8)
    ap.add_argument("--color1", default="FE0107")
    ap.add_argument("--color2", default="00E6FF")
    ap.add_argument("--press", default="0:8-20,4:28-40",
                    help="按键脚本，例如 0:8-20,4:28-40（键:起始帧-结束帧）")
    ap.add_argument("--out", default="out")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for k in sorted(EFFECT_NAMES):
            print("%2d  %s" % (k, EFFECT_NAMES[k]))
        return

    script = []
    if args.press:
        for part in args.press.split(","):
            key, rng = part.split(":")
            start, end = (int(x) for x in rng.split("-"))
            script.append((start, end, int(key)))

    st = State(args.effect, args.speed, args.fps, parse_color(args.color1), parse_color(args.color2))
    frames = []
    masks = []
    for f in range(args.frames):
        keys = 0
        for (start, end, key) in script:
            if start <= f <= end:
                keys |= 1 << key
        st.step(keys)
        frames.append(list(st.leds))
        masks.append(keys)

    os.makedirs(args.out, exist_ok=True)
    render(frames, os.path.join(args.out, "frames.png"))

    with open(os.path.join(args.out, "timeline.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["frame"] + ["led%d_r,led%d_g,led%d_b" % (i, i, i) for i in range(LEDS)])
        for idx, frame in enumerate(frames):
            row = [idx]
            for c in frame:
                row.extend(c)
            w.writerow(row)

    worst_all, where_all = continuity(frames)
    worst, where = continuity(frames, masks)
    report = []
    report.append("effect: %d (%s)" % (args.effect, EFFECT_NAMES.get(args.effect, "?")))
    report.append("frames: %d  fps: %d  speed: %d  dt: %.1f ms" % (args.frames, args.fps, args.speed, st.dt))
    report.append("color1: #%02X%02X%02X   color2: #%02X%02X%02X" % (st.c1 + st.c2))
    report.append("max delta incl. key edges : %d (led %d, frame %d)" % (
        worst_all, where_all[0] if where_all else -1, where_all[1] if where_all else -1))
    report.append("max delta between edges  : %d (led %d, frame %d)" % (
        worst, where[0] if where else -1, where[1] if where else -1))
    if where:
        i, f = where
        report.append("  color change: %s -> %s" % (frames[f - 1][i], frames[f][i]))
    # 涟漪/彗星这种有"波前"的效果，波前扫过某一格时本来就是一帧内从暗到亮，
    # 这不算跳变；只对没有波前的效果报 JUMP。
    front_effects = (8, 9, 14, 19, 22)
    if worst <= 40:
        verdict = "smooth"
    elif args.effect in front_effects:
        verdict = "smooth (wave front step is expected)"
    else:
        verdict = "JUMP - investigate"
    report.append("verdict: %s" % verdict)
    text = "\n".join(report)
    with open(os.path.join(args.out, "report.txt"), "w") as fh:
        fh.write(text + "\n")
    print(text)
    print("images: %s" % os.path.join(args.out, "frames.png"))


if __name__ == "__main__":
    main()
