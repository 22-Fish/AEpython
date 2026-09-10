"""逐字符打印每一行的所有字符格式，排查整体偏移"""
import sys, os

# 开发脚本位于 tools/，需要把 src 目录加入 sys.path 才能导入项目模块
_项目根 = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_项目根, "src"))

from PyQt5.QtWidgets import QApplication, QPlainTextEdit
from PyQt5.QtCore import QTimer
from GUI.语法高亮器 import Python语法高亮器

app = QApplication(sys.argv)

文件路径 = sys.argv[1] if len(sys.argv) > 1 else ""
if not 文件路径 or not os.path.exists(文件路径):
    print(f"用法: python debug_highlight.py <文件路径>")
    sys.exit(1)

edit = QPlainTextEdit()
with open(文件路径, 'r', encoding='utf-8') as f:
    content = f.read()
edit.setPlainText(content)
hl = Python语法高亮器(edit.document())

def 获取格式(位置, formats):
    for fmt_range in formats:
        fs = fmt_range.start
        fc = fmt_range.length
        ff = fmt_range.format
        if fs <= 位置 < fs + fc:
            tip = ff.toolTip() if ff.toolTip() else 'none'
            color = ff.foreground().color().name() if ff.foreground().color().isValid() else 'invalid'
            return tip, color
    return 'def', 'n/a'

def check():
    doc = edit.document()
    
    for i in range(doc.blockCount()):
        block = doc.findBlockByNumber(i)
        text = block.text()
        if not text.strip():
            continue
        
        l = len(text)
        layout = block.layout()
        formats = layout.additionalFormats() if layout else []
        
        # 统计每个字符的格式
        chars = []
        tips = []
        for j in range(l):
            ch = text[j]
            tip, color = 获取格式(j, formats)
            chars.append(ch)
            tips.append(tip[:4])
        
        # 排成两行比较
        char_line = ''.join(c if ord(c) < 128 else '·' for c in chars)
        print(f"行{i:2d}: {char_line}")
        print(f"       {''.join(f'{t:4s}' for t in tips)}")
        
        # 找变化点
        print(f"  格式段:")
        prev_tip = None
        seg_start = 0
        for j in range(l):
            tip, _ = 获取格式(j, formats)
            if tip != prev_tip:
                if prev_tip is not None:
                    print(f"    [{seg_start}-{j-1}] {prev_tip}: {repr(text[seg_start:j])}")
                seg_start = j
                prev_tip = tip
        if seg_start < l:
            print(f"    [{seg_start}-{l-1}] {prev_tip}: {repr(text[seg_start:l])}")
        print()
    
    sys.exit(0)

QTimer.singleShot(500, check)
sys.exit(app.exec_())
