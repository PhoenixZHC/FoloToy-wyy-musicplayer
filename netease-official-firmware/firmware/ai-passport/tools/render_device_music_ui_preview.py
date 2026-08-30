from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


W = 240
H = 320
BG = "#151821"
BAR = "#101216"
DIVIDER = "#242A35"
CARD = "#202637"
ERROR_CARD = "#251B22"
TEXT = "#FFFFFF"
MUTED = "#C7CEDB"
SUBTLE = "#9CA3AF"
RED = "#E60026"
GREEN = "#19C37D"
BLUE = "#60A5FA"
AMBER = "#FBBF24"
DANGER = "#EF4444"


FONT_PATH = Path("C:/Windows/Fonts/NotoSansSC-VF.ttf")
BOLD_FONT_PATH = Path("C:/Windows/Fonts/HarmonyOS_Sans_SC_Bold.ttf")


def font(size: int) -> ImageFont.FreeTypeFont:
    path = BOLD_FONT_PATH if size in {17, 18, 22, 24} else FONT_PATH
    return ImageFont.truetype(str(path), size=size)


def text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], value: str, size: int, fill: str,
         box: tuple[int, int] | None = None, anchor: str | None = None) -> None:
    if box is None:
        draw.text(xy, value, font=font(size), fill=fill, anchor=anchor)
        return
    x, y = xy
    bw, _ = box
    for i, line in enumerate(value.splitlines()):
        bbox = draw.textbbox((0, 0), line, font=font(size))
        tw = bbox[2] - bbox[0]
        tx = x
        if anchor == "center":
            tx = x + (bw - tw) // 2
        elif anchor == "right":
            tx = x + bw - tw
        draw.text((tx, y + i * (size + 4)), line, font=font(size), fill=fill)


def base(chip: str, chip_color: str, footer: tuple[str, str, str]) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 240, 34), fill=BAR)
    d.rounded_rectangle((10, 10, 22, 22), radius=6, fill=RED)
    d.ellipse((14, 14, 18, 18), fill=TEXT)
    text(d, (28, 6), "网易云音乐", 14, TEXT)
    text(d, (146, 8), chip, 10, chip_color, (56, 13), "right")
    d.rounded_rectangle((210, 10, 228, 19), radius=2, fill="#2A2F3A")
    d.rectangle((228, 13, 230, 16), fill="#6B7280")
    d.rounded_rectangle((212, 12, 224, 17), radius=1, fill=GREEN)
    d.rectangle((0, 268, 240, 320), fill=BAR)
    d.rectangle((0, 268, 240, 269), fill=DIVIDER)
    for x, key, label in [(10, "UP", footer[0]), (86, "OK", footer[1]), (164, "DN", footer[2])]:
        d.rounded_rectangle((x + 18, 280, x + 48, 297), radius=4, fill="#F3F4F6")
        text(d, (x + 18, 282), key, 9, "#111318", (30, 12), "center")
        text(d, (x, 301), label, 12, MUTED, (66 if x != 86 else 70, 16), "center")
    return img


def qr(d: ImageDraw.ImageDraw, x: int, y: int) -> None:
    d.rounded_rectangle((x, y, x + 136, y + 136), radius=8, fill="#F8FAFC")
    d.rectangle((x + 12, y + 12, x + 124, y + 124), fill="#FFFFFF")
    for fx, fy in [(21, 21), (89, 21), (21, 89)]:
        d.rectangle((x + fx, y + fy, x + fx + 26, y + fy + 26), fill="#111318")
        d.rectangle((x + fx + 5, y + fy + 5, x + fx + 21, y + fy + 21), fill="#FFFFFF")
        d.rectangle((x + fx + 10, y + fy + 10, x + fx + 16, y + fy + 16), fill="#111318")
    cells = [(58, 22), (70, 22), (82, 28), (57, 38), (76, 40), (92, 50),
             (47, 58), (64, 58), (74, 68), (88, 70), (102, 76), (46, 82),
             (58, 86), (74, 86), (88, 94), (105, 104), (68, 106), (82, 113),
             (99, 114), (109, 88), (105, 62), (32, 72)]
    for cx, cy in cells:
        d.rectangle((x + cx, y + cy, x + cx + 6, y + cy + 6), fill="#111318")


def progress_screen(chip: str, title: str, detail: str, footer: tuple[str, str, str],
                    y: int, active: int, name: str) -> tuple[str, Image.Image]:
    img = base(chip, BLUE, footer)
    d = ImageDraw.Draw(img)
    d.ellipse((89, y, 151, y + 62), outline="#343A46", width=5)
    d.arc((89, y, 151, y + 62), 270, 35, fill=RED, width=5)
    text(d, (16, 154), title, 22, TEXT, (208, 28), "center")
    text(d, (20, 190), detail, 14, MUTED, (200, 60), "center")
    for i in range(4):
        fill = RED if i < active else "#343A46"
        d.rounded_rectangle((84 + i * 18, 245, 94 + i * 18, 249), radius=2, fill=fill)
    return name, img


def list_screen(chip: str, heading: str, count: str, rows: list[tuple[str, str, bool]],
                footer: tuple[str, str, str], name: str) -> tuple[str, Image.Image]:
    img = base(chip, RED, footer)
    d = ImageDraw.Draw(img)
    text(d, (14, 48), heading, 17, TEXT)
    text(d, (186, 52), count, 10, SUBTLE, (34, 13), "right")
    for i, (title, sub, selected) in enumerate(rows[:5]):
        y = 78 + i * 36
        bg = RED if selected else CARD
        d.rounded_rectangle((12, y, 228, y + 33), radius=6, fill=bg)
        text(d, (24, y + 5), title, 13, TEXT if selected else "#E5E7EB")
        text(d, (24, y + 20), sub, 8, "#FFE1E7" if selected else SUBTLE)
        if selected:
            text(d, (180, y + 9), "播放", 10, TEXT, (34, 13), "right")
    return name, img


def render_all(out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    screens: list[tuple[str, Image.Image]] = []
    img = base("未配置", "#F59E0B", ("无", "确认", "无"))
    d = ImageDraw.Draw(img)
    text(d, (14, 56), "连接设备热点", 22, TEXT, (212, 28), "center")
    d.rounded_rectangle((16, 100, 224, 184), radius=8, fill=CARD)
    text(d, (30, 112), "设备热点", 12, "#8B94A7")
    text(d, (30, 134), "WY-MUSIC-7A3F", 18, TEXT)
    text(d, (20, 204), "打开浏览器进入 192.168.4.1\n填写 Wi-Fi、服务地址和注册码", 13, MUTED, (200, 36), "center")
    screens.append(("01-setup.png", img))
    screens.append(progress_screen("注册中", "正在连接", "加入 Wi-Fi\n注册设备\n检查音乐接口", ("无", "等待", "无"), 72, 2, "02-registering.png"))
    img = base("就绪", GREEN, ("无", "进入", "无"))
    d = ImageDraw.Draw(img)
    d.ellipse((82, 62, 158, 138), fill=GREEN)
    d.rounded_rectangle((105, 101, 115, 125), radius=5, fill=TEXT)
    d.rounded_rectangle((118, 83, 128, 125), radius=5, fill=TEXT)
    text(d, (16, 154), "设备已绑定", 22, TEXT, (208, 28), "center")
    text(d, (20, 194), "设备注册成功\n按 OK 进入音乐应用", 14, MUTED, (200, 40), "center")
    screens.append(("03-ready.png", img))
    img = base("失败", DANGER, ("无", "重试", "清空"))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((18, 58, 222, 212), radius=8, fill=ERROR_CARD)
    text(d, (32, 74), "配置失败", 22, "#FCA5A5")
    text(d, (32, 118), "Wi-Fi 或服务不可用\n短按重试\n长按清空配置", 14, "#F3D3D8")
    screens.append(("04-setup-failed.png", img))
    img = base("登录", "#F87171", ("无", "刷新", "清空"))
    d = ImageDraw.Draw(img)
    qr(d, 52, 50)
    text(d, (16, 196), "扫码登录网易云", 18, TEXT, (208, 23), "center")
    text(d, (20, 224), "手机打开网易云音乐扫一扫", 12, MUTED, (200, 16), "center")
    screens.append(("05-qr-login.png", img))
    img = base("等待", AMBER, ("无", "等待", "清空"))
    d = ImageDraw.Draw(img)
    qr(d, 52, 46)
    d.rounded_rectangle((36, 188, 204, 222), radius=17, fill="#2A2130")
    text(d, (46, 194), "请在手机上确认登录", 14, TEXT, (148, 18), "center")
    text(d, (20, 236), "设备会自动刷新状态", 12, MUTED, (200, 16), "center")
    screens.append(("06-waiting.png", img))
    screens.append(list_screen("歌单", "选择歌单", "1/6",
                               [("每日推荐", "30 首", True), ("我喜欢的音乐", "128 首", False),
                                ("通勤电台", "42 首", False), ("深夜循环", "18 首", False),
                                ("老歌收藏", "64 首", False)],
                               ("上移", "打开", "下移"), "07-playlists.png"))
    screens.append(list_screen("歌曲", "每日推荐", "2/5",
                               [("蓝色时刻", "陈粒", False), ("城市灯光", "落日飞车", True),
                                ("雨后", "告五人", False), ("Soft Reset", "Radio Mix", False),
                                ("晚风", "Demo Track", False)],
                               ("上移", "播放", "下移"), "08-tracks.png"))
    screens.append(progress_screen("加载", "准备播放", "获取播放地址\n启动音频流", ("无", "等待", "无"), 70, 2, "09-loading.png"))
    img = base("播放中", GREEN, ("上一首", "暂停", "下一首"))
    d = ImageDraw.Draw(img)
    for i, (x, y, h) in enumerate([(58, 112, 20), (76, 90, 42), (94, 104, 28), (112, 78, 54), (130, 98, 34), (148, 84, 48), (166, 108, 24)]):
        d.rounded_rectangle((x, y, x + 10, y + h), radius=5, fill=TEXT if i % 2 == 0 else RED)
    text(d, (16, 152), "城市灯光", 24, TEXT, (208, 30), "center")
    text(d, (20, 188), "落日飞车 · 128 kbps", 13, MUTED, (200, 17), "center")
    d.rounded_rectangle((32, 230, 208, 235), radius=3, fill="#343A46")
    d.rounded_rectangle((32, 230, 106, 235), radius=3, fill=RED)
    text(d, (32, 240), "01:12", 10, SUBTLE)
    text(d, (156, 240), "03:46", 10, SUBTLE, (52, 13), "right")
    screens.append(("10-playing.png", img))
    img = base("暂停", AMBER, ("上一首", "继续", "下一首"))
    d = ImageDraw.Draw(img)
    d.ellipse((72, 58, 168, 154), outline=RED, width=5)
    d.rounded_rectangle((101, 90, 113, 128), radius=4, fill=TEXT)
    d.rounded_rectangle((126, 90, 138, 128), radius=4, fill=TEXT)
    text(d, (16, 166), "城市灯光", 24, TEXT, (208, 30), "center")
    text(d, (20, 202), "播放已暂停", 13, MUTED, (200, 17), "center")
    screens.append(("11-paused.png", img))
    img = base("失败", DANGER, ("无", "重试", "清空"))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((18, 58, 222, 212), radius=8, fill=ERROR_CARD)
    text(d, (32, 74), "播放失败", 22, "#FCA5A5")
    text(d, (32, 118), "播放地址不可用\n短按重新登录\n长按清空配置", 14, "#F3D3D8")
    screens.append(("12-play-failed.png", img))

    written: list[Path] = []
    for name, img in screens:
        path = out_dir / name
        img.save(path)
        written.append(path)
    sheet = Image.new("RGB", (4 * W, 3 * H), "#0B0D12")
    for i, (_, img) in enumerate(screens):
        sheet.paste(img, ((i % 4) * W, (i // 4) * H))
    sheet_path = out_dir / "contact-sheet.png"
    sheet.save(sheet_path)
    written.append(sheet_path)
    return written


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="build/device_music_ui_preview")
    args = parser.parse_args()
    for path in render_all(Path(args.out)):
        print(path)


if __name__ == "__main__":
    main()
