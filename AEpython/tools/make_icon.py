# make_icon.py - 由 assets/AEpy.svg 生成 PNG 预览与多尺寸 ICO
"""
用法（在项目根目录执行）：
    .venv\\Scripts\\python.exe tools\\make_icon.py

生成：
    assets/AEpy.png    256x256 预览图
    assets/AEpy.ico    多尺寸图标（16/24/32/48/64/128/256）

ICO 采用传统的 32bpp BMP 条目（而不是 PNG 压缩条目），
这样 Windows 资源管理器与 windres（编译 AEpy.exe 的图标资源）都能可靠识别。
"""
import os
import struct
import sys

from PyQt5.QtCore import QByteArray, QSize, Qt
from PyQt5.QtGui import QGuiApplication, QImage, QPainter
from PyQt5.QtSvg import QSvgRenderer

项目根 = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG路径 = os.path.join(项目根, "assets", "AEpy.svg")
PNG路径 = os.path.join(项目根, "assets", "AEpy.png")
ICO路径 = os.path.join(项目根, "assets", "AEpy.ico")

# ICO 中包含的尺寸（像素）
尺寸列表 = (16, 24, 32, 48, 64, 128, 256)


def 载入渲染器():
    """读取 SVG 并构造渲染器"""
    with open(SVG路径, "rb") as f:
        数据 = f.read()
    渲染器 = QSvgRenderer(QByteArray(数据))
    if not 渲染器.isValid():
        raise RuntimeError(f"SVG 解析失败：{SVG路径}")
    return 渲染器


def 渲染(渲染器, 边长):
    """把 SVG 渲染成 边长×边长 的 ARGB32 图像（透明背景）"""
    图像 = QImage(QSize(边长, 边长), QImage.Format_ARGB32)
    图像.fill(Qt.transparent)
    画笔 = QPainter(图像)
    画笔.setRenderHint(QPainter.Antialiasing, True)
    画笔.setRenderHint(QPainter.SmoothPixmapTransform, True)
    渲染器.render(画笔)
    画笔.end()
    return 图像


def 图像转DIB(图像):
    """把 ARGB32 图像转成 ICO 内嵌的 DIB：BITMAPINFOHEADER + XOR 位图 + AND 掩码"""
    宽 = 图像.width()
    高 = 图像.height()

    异或 = bytearray()
    for y in range(高 - 1, -1, -1):  # BMP 位图自下而上存放
        for x in range(宽):
            像素 = 图像.pixel(x, y)
            异或.append(像素 & 0xFF)          # 蓝
            异或.append((像素 >> 8) & 0xFF)   # 绿
            异或.append((像素 >> 16) & 0xFF)  # 红
            异或.append((像素 >> 24) & 0xFF)  # Alpha

    # AND 掩码：1 位/像素，行按 4 字节对齐，标记完全透明的像素
    掩码行字节 = ((宽 + 31) // 32) * 4
    掩码 = bytearray(掩码行字节 * 高)
    for 输出行 in range(高):
        源行 = 高 - 1 - 输出行
        for x in range(宽):
            透明度 = (图像.pixel(x, 源行) >> 24) & 0xFF
            if 透明度 == 0:
                掩码[输出行 * 掩码行字节 + x // 8] |= 0x80 >> (x % 8)

    头 = struct.pack(
        "<IiiHHIIiiII",
        40,                                  # biSize
        宽,                                  # biWidth
        高 * 2,                              # biHeight（XOR 与 AND 各占一份）
        1,                                   # biPlanes
        32,                                  # biBitCount
        0,                                   # biCompression = BI_RGB
        len(异或) + len(掩码),                # biSizeImage
        0, 0, 0, 0,
    )
    return bytes(头) + bytes(异或) + bytes(掩码)


def 写ICO(条目, 路径):
    """条目为 [(边长, DIB数据), ...]，写出 ICONDIR + 目录表 + 图像数据"""
    数量 = len(条目)
    目录 = bytearray()
    数据区 = bytearray()
    偏移 = 6 + 16 * 数量
    for 边长, 数据 in 条目:
        边长字段 = 0 if 边长 >= 256 else 边长  # 256 在目录中用 0 表示
        目录 += struct.pack("<BBBBHHII", 边长字段, 边长字段, 0, 0, 1, 32, len(数据), 偏移)
        数据区 += 数据
        偏移 += len(数据)
    with open(路径, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, 数量))  # reserved, type=1(ICON), count
        f.write(bytes(目录))
        f.write(bytes(数据区))


def main():
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    QGuiApplication.instance() or QGuiApplication(sys.argv[:1])

    渲染器 = 载入渲染器()

    预览 = 渲染(渲染器, 256)
    if not 预览.save(PNG路径, "PNG"):
        raise RuntimeError(f"写入 PNG 失败：{PNG路径}")

    条目 = [(边长, 图像转DIB(渲染(渲染器, 边长))) for 边长 in 尺寸列表]
    写ICO(条目, ICO路径)

    尺寸文本 = "/".join(str(s) for s in 尺寸列表)
    print(f"已生成 {PNG路径}（256x256）")
    print(f"已生成 {ICO路径}（{len(条目)} 个尺寸：{尺寸文本}，"
          f"{os.path.getsize(ICO路径) / 1024:.1f} KB）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
