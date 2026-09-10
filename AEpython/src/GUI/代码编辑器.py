# 代码编辑器.py - 修复字号保存问题，增加调试日志
import sys
import logging
import os
import json
from PyQt5.QtWidgets import (
    QPlainTextEdit, QTextEdit, QWidget, QMenu, QAction, QApplication
)
from PyQt5.QtGui import (
    QFont, QColor, QPainter, QTextFormat, QKeySequence,
    QTextCharFormat, QTextCursor, QPalette
)
from PyQt5.QtCore import Qt, QRect, QRectF, QSize, QTimer

from config.路径 import 配置文件路径

logging.basicConfig(
    stream=sys.stderr,
    level=logging.DEBUG,
    format='[%(asctime)s] %(levelname)s: [代码编辑器] %(message)s'
)
logger = logging.getLogger(__name__)


class 行号区域(QWidget):
    def __init__(self, 编辑器):
        super().__init__(编辑器)
        self.编辑器 = 编辑器

    def sizeHint(self):
        return QSize(self.编辑器.计算行号宽度(), 0)

    def paintEvent(self, event):
        self.编辑器.绘制行号(event)


class 代码编辑器类(QPlainTextEdit):
    def __init__(self, 父=None):
        super().__init__(父)

        # 字体配置初始值
        self.当前字体族 = "Microsoft YaHei"
        self.当前字号 = 12
        self.行号字体 = QFont("Microsoft YaHei", 9)

        # 基础设置
        self.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.setTabStopDistance(self.fontMetrics().width(' ') * 4)
        self.setTabChangesFocus(False)
        # 光标宽度设为 0：彻底隐藏 Qt 自己绘制的默认光标，只保留下面 paintEvent 中的自绘光标
        # （输入法定位所需的非零矩形由 inputMethodQuery 补上）
        self.光标宽度 = 2
        self.setCursorWidth(0)
        self.setContextMenuPolicy(Qt.DefaultContextMenu)

        # 行号区域
        self.行号区 = 行号区域(self)
        self.blockCountChanged.connect(lambda c: self.更新行号区宽度(c))
        self.updateRequest.connect(lambda rect, dy: self.刷新行号区(rect, dy))
        self.cursorPositionChanged.connect(lambda: self.高亮当前行())
        self.更新行号区宽度(0)

        # 加载字体配置（会从文件读取字号，若无则写入默认）
        self._加载字体配置()

        # 颜色
        self.颜色配置 = {}
        self.默认背景色 = "#ffffff"
        self.当前行颜色 = "#ffff99"
        self.选中区背景 = "#3399ff"
        self.选中区文字 = ""
        self.行号文字色 = "#000000"
        self.行号背景色 = "#e0e0e0"
        self.光标颜色 = "#000000"
        self._应用光标颜色()

        # 括号高亮
        self.括号高亮列表 = None
        self.括号高亮定时器 = QTimer(self)
        self.括号高亮定时器.setSingleShot(True)
        self.括号高亮定时器.timeout.connect(self._clear_bracket_highlight)
        self.括号输入标志 = False  # True 表示刚输入了括号字符，才触发匹配

        self.document().contentsChanged.connect(self._on_text_changed)

        self.高亮当前行()
        logger.info("代码编辑器初始化完成（Ctrl+滚轮调整字号，已修复保存）")

    # ---------- 字体配置加载与保存 ----------
    def _obter_config_path(self):
        """获取字体配置文件路径（config/font.json）"""
        return 配置文件路径("font.json")

    def _加载字体配置(self):
        """从 config/font.json 加载字体族和字号，若无则创建默认并保存"""
        config_path = self._obter_config_path()
        logger.debug(f"[调试] _加载字体配置 开始，当前 self.当前字号={self.当前字号}")
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                config = json.load(f)
                self.当前字体族 = config.get("font", "Microsoft YaHei")
                self.当前字号 = config.get("font_size", 12)
                logger.info(f"从配置文件加载字体: 字体族={self.当前字体族}, 字号={self.当前字号}")
        except (FileNotFoundError, json.JSONDecodeError) as e:
            logger.warning(f"读取字体配置失败({e})，使用默认值并保存")
            self.当前字体族 = "Microsoft YaHei"
            self.当前字号 = 12
            self._保存字体配置()  # 创建默认配置文件

        logger.debug(f"[调试] _加载字体配置 完成，self.当前字号={self.当前字号}，即将调用 _应用字体")
        self._应用字体()

    def _保存字体配置(self):
        """保存当前字体族和字号到 config/font.json"""
        config_path = self._obter_config_path()
        config_dir = os.path.dirname(config_path)
        logger.debug(f"[调试] _保存字体配置 开始，self.当前字号={self.当前字号}")
        try:
            os.makedirs(config_dir, exist_ok=True)
            config = {
                "font": self.当前字体族,
                "font_size": self.当前字号
            }
            with open(config_path, "w", encoding="utf-8") as f:
                json.dump(config, f, indent=2, ensure_ascii=False)
            logger.info(f"字体配置已保存: {config}")
        except Exception as e:
            logger.error(f"保存字体配置失败: {e}")

    def _应用字体(self):
        """应用当前字体族和字号到编辑器，并同步更新行号字体"""
        logger.debug(f"[调试] _应用字体 开始，self.当前字号={self.当前字号}")
        编辑字体 = QFont()
        编辑字体.setFamilies([self.当前字体族, "Microsoft YaHei"])
        编辑字体.setPointSize(self.当前字号)
        self.setFont(编辑字体)
        # 验证是否设置成功
        实际字号 = self.font().pointSize()
        logger.debug(f"[调试] _应用字体: 设置后 self.font().pointSize()={实际字号}")

        行号字号 = max(8, self.当前字号 - 2)
        self.行号字体 = QFont(self.当前字体族, 行号字号)

        # 强制更新行号区
        self.行号区.update()
        QTimer.singleShot(0, lambda: self.更新行号区宽度(0))

        logger.info(f"应用字体: 字体族={self.当前字体族}, 请求字号={self.当前字号}, 实际生效={实际字号}, 行号字号={行号字号}")

    def 设置字体大小(self, 增量):
        """调整字体大小（增量可为正或负），并保存配置"""
        新字号 = self.当前字号 + 增量
        logger.debug(f"[调试] 设置字体大小: 增量={增量}, 当前={self.当前字号}, 计算新值={新字号}")
        if 新字号 < 6:
            新字号 = 6
            logger.debug(f"[调试] 字号低于最小值，限制为 {新字号}")
        if 新字号 > 72:
            新字号 = 72
            logger.debug(f"[调试] 字号超过最大值，限制为 {新字号}")
        if 新字号 == self.当前字号:
            logger.debug(f"字号未变化，当前={self.当前字号}")
            return
        self.当前字号 = 新字号
        logger.debug(f"[调试] self.当前字号 已更新为 {self.当前字号}，即将调用 _应用字体")
        self._应用字体()
        self._保存字体配置()
        logger.info(f"字体大小已调整为: {self.当前字号}")

    def 重新加载字体配置(self):
        """供外部调用，重新从配置加载字体（用于样式对话框修改字体族后）"""
        logger.debug(f"[调试] 重新加载字体配置 被调用，当前 self.当前字号={self.当前字号}")
        self._加载字体配置()
        logger.debug(f"[调试] 重新加载字体配置 完成，self.当前字号={self.当前字号}")

    # ---------- 滚轮事件：Ctrl+滚轮调整字号 ----------
    def wheelEvent(self, event):
        modifiers = event.modifiers()
        logger.debug(f"wheelEvent: modifiers={int(modifiers)} (Ctrl={int(Qt.ControlModifier)}), angleDelta={event.angleDelta()}")

        if modifiers & Qt.ControlModifier:
            delta = event.angleDelta().y()
            if delta == 0:
                # 尝试像素滚动
                delta = event.pixelDelta().y()
                if delta != 0:
                    delta = 120 if delta > 0 else -120
            if delta != 0:
                if delta > 0:
                    self.设置字体大小(1)
                else:
                    self.设置字体大小(-1)
                event.accept()
                return

        # 未按 Ctrl 或 delta=0 时，正常滚动
        super().wheelEvent(event)

    # ---------- 行号相关 ----------
    def 计算行号宽度(self):
        digits = len(str(max(1, self.blockCount())))
        return 10 + self.fontMetrics().width('9') * digits

    def 更新行号区宽度(self, _):
        self.setViewportMargins(self.计算行号宽度(), 0, 0, 0)

    def 刷新行号区(self, rect, dy):
        if dy:
            self.行号区.scroll(0, dy)
        else:
            self.行号区.update(0, rect.y(), self.行号区.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self.更新行号区宽度(0)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        cr = self.contentsRect()
        self.行号区.setGeometry(QRect(cr.left(), cr.top(), self.计算行号宽度(), cr.height()))

    def 绘制行号(self, event):
        painter = QPainter(self.行号区)
        painter.fillRect(event.rect(), QColor(self.行号背景色))
        painter.setFont(self.行号字体)

        right_pad = 10
        block = self.firstVisibleBlock()
        block_number = block.blockNumber()
        top = self.blockBoundingGeometry(block).translated(self.contentOffset()).top()
        bottom = top + self.blockBoundingRect(block).height()

        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                number = str(block_number + 1)
                painter.setPen(QColor(self.行号文字色))
                painter.drawText(
                    0, int(top),
                    self.行号区.width() - right_pad,
                    self.fontMetrics().height(),
                    Qt.AlignRight | Qt.AlignVCenter,
                    number
                )
            block = block.next()
            top = bottom
            bottom = top + self.blockBoundingRect(block).height()
            block_number += 1

    def 高亮当前行(self):
        extras = []
        # 1. 当前行底色（优先级最低）
        if not self.isReadOnly():
            line_extra = QTextEdit.ExtraSelection()
            line_fmt = QTextCharFormat()
            line_fmt.setBackground(QColor(self.当前行颜色))
            line_fmt.setProperty(QTextFormat.FullWidthSelection, True)
            line_extra.format = line_fmt
            line_extra.cursor = self.textCursor()
            line_extra.cursor.clearSelection()
            extras.append(line_extra)
        # 2. 选中区背景（由 Qt 自动绘制，此处不再手动覆盖）
        # 3. 括号高亮（最高优先级，因为里面设了 foreground）
        if self.括号高亮列表:
            extras.extend(self.括号高亮列表)
        self.setExtraSelections(extras)

    def _应用光标颜色(self):
        """通过 palette 控制编辑器背景。光标颜色用自定义绘制覆盖。"""
        palette = self.palette()
        palette.setColor(QPalette.Base, QColor(self.默认背景色))
        # 选中区背景由 Qt 自动绘制
        palette.setColor(QPalette.Highlight, QColor(self.选中区背景))
        self.setPalette(palette)
        self.viewport().setPalette(palette)
        # 光标闪烁定时器（只创建一次）
        if getattr(self, '_cursor_timer', None) is None:
            self.光标闪动开关 = True
            self._cursor_timer = QTimer(self)
            self._cursor_timer.timeout.connect(self._tog_cursor)
            self._cursor_timer.start(530)

    def paintEvent(self, event):
        # 画所有内容（文字、语法高亮、ExtraSelections（当前行、选中区、括号高亮））
        super().paintEvent(event)
        # 默认光标已通过 setCursorWidth(0) 隐藏，这里只画自定义光标（带闪烁）
        painter = QPainter(self.viewport())
        cursor = self.textCursor()
        if not self.isReadOnly() and self.hasFocus():
            rect = self.cursorRect(cursor)
            if rect.height() > 0 and rect.x() >= 0 and self.光标闪动开关:
                painter.setPen(Qt.NoPen)
                painter.setBrush(QColor(self.光标颜色))
                painter.drawRect(rect.x(), rect.y(), self.光标宽度, rect.height())
        painter.end()

    def _tog_cursor(self):
        self.光标闪动开关 = not self.光标闪动开关
        self.viewport().update()

    # ---------- 颜色配置 ----------
    def 更新颜色配置(self, 颜色配置):
        self.颜色配置 = 颜色配置
        self.默认背景色 = 颜色配置.get("默认背景色", "#ffffff")
        self.当前行颜色 = 颜色配置.get("当前行背景色", "#ffff99")
        选中区配置 = 颜色配置.get("选中区", {})
        self.选中区背景 = 选中区配置.get("背景色", "#3399ff")
        启用自定义文字色 = 选中区配置.get("启用自定义文字色", False)
        if 启用自定义文字色:
            self.选中区文字 = 选中区配置.get("文字色", "#ffffff")
            self._选中区文字自定义 = True
        else:
            self.选中区文字 = "#ffffff"  # 不自定义时也设为白色，确保高亮可见
            self._选中区文字自定义 = False
        self.行号文字色 = 颜色配置.get("行号", {}).get("文字色", "#000000")
        self.行号背景色 = 颜色配置.get("行号", {}).get("背景色", "#e0e0e0")
        self.光标颜色 = 颜色配置.get("光标", {}).get("文字色", "#000000")
        self._应用光标颜色()
        # 同步更新 Qt 自动选中区颜色
        palette = self.palette()
        palette.setColor(QPalette.Highlight, QColor(self.选中区背景))
        self.setPalette(palette)
        self.viewport().setPalette(palette)
        self.行号区.update()
        self.高亮当前行()

    # ---------- 输入法 ----------
    def inputMethodQuery(self, query):
        """默认光标宽度为 0，这里给输入法返回一个非零宽的候选框定位矩形"""
        if query == Qt.ImCursorRectangle:
            rect = self.cursorRect()
            if rect.width() < self.光标宽度:
                rect.setWidth(self.光标宽度)
            return QRectF(rect)
        return super().inputMethodQuery(query)

    # ---------- 括号高亮 ----------
    def _on_text_changed(self):
        QTimer.singleShot(50, self._detect_bracket)

    def _detect_bracket(self):
        try:
            # 只有真正输入了括号字符才触发匹配
            if not self.括号输入标志:
                return
            self.括号输入标志 = False

            cursor = self.textCursor()
            pos = cursor.position()
            if pos == 0:
                self._clear_bracket_highlight()
                return
            block = cursor.block()
            line = block.text()
            col = pos - block.position()

            idx = -1
            ch = None

            if col > 0:
                前一个 = line[col - 1]
                if 前一个 in '([':
                    # 输入了左括号：取 col-1（刚输入的这个左括号）
                    idx = col - 1
                    ch = 前一个
                elif 前一个 in ')]}':
                    if col < len(line) and line[col] == 前一个:
                        # 输入了右括号，且光标位置也是同类型右括号
                        # 优先匹配光标位置的旧括号
                        idx = col
                        ch = line[col]
                    else:
                        # 输入了右括号，光标后无旧括号
                        idx = col - 1
                        ch = 前一个
                else:
                    self._clear_bracket_highlight()
                    return
            else:
                self._clear_bracket_highlight()
                return

            if ch in '([{}])':
                self._do_match_bracket(ch, explicit_idx=idx)
            else:
                self._clear_bracket_highlight()
        except Exception as e:
            logger.error(f"检测括号异常: {e}")

    def _do_match_bracket(self, bracket_char, explicit_idx=None):
        """
        标准栈匹配：先对整行做栈解析生成配对表，然后查表匹配。
        explicit_idx 指定具体的括号位置（来自 _detect_bracket 的检测结果）。
        """
        try:
            self._clear_bracket_highlight()
            doc = self.document()
            cursor = self.textCursor()
            pos = cursor.position()
            block = doc.findBlock(pos)
            if not block.isValid():
                return
            line = block.text()

            if explicit_idx is not None:
                idx = explicit_idx
            else:
                col = pos - block.position()
                idx = col - 1

            if idx < 0 or idx >= len(line) or line[idx] != bracket_char:
                return

            # 生成整行的配对表：left_pos -> right_pos
            stack = []  # 存 (左括号类型, 位置)
            pair_map = {}  # 左括号位置 -> 右括号位置
            pair_rev = {}  # 右括号位置 -> 左括号位置
            pairs = {'(': ')', '[': ']', '{': '}'}

            for i, ch in enumerate(line):
                if ch in '([{':
                    stack.append((ch, i))
                elif ch in ')]}':
                    if stack:
                        left_type, left_pos = stack.pop()
                        if pairs[left_type] == ch:
                            pair_map[left_pos] = i
                            pair_rev[i] = left_pos
                        else:
                            # 类型不匹配，清空栈（Python 语法中不同类型不配对）
                            stack.clear()
                    # 栈为空时右括号无法配对，pair_rev 中不记录

            # 查表
            left_pos = None
            right_pos = None

            if bracket_char in '([{':
                # 左括号 -> 向右找配对的右括号
                if idx in pair_map:
                    left_pos = idx
                    right_pos = pair_map[idx]
            else:
                # 右括号 -> 向左找配对的左括号
                if idx in pair_rev:
                    left_pos = pair_rev[idx]
                    right_pos = idx

            if left_pos is None or right_pos is None:
                # 无法配对，不显示高亮
                return

            left_abs = block.position() + left_pos
            right_abs = block.position() + right_pos

            doc_len = doc.characterCount() - 1
            if left_abs < 0 or right_abs >= doc_len or left_abs >= right_abs:
                return

            fmt = QTextCharFormat()
            fmt.setBackground(QColor(self.颜色配置.get("括号匹配", {}).get("背景色", "#808080")))
            extra = QTextEdit.ExtraSelection()
            extra.format = fmt
            c = QTextCursor(doc)
            c.setPosition(left_abs)
            c.movePosition(QTextCursor.Right, QTextCursor.KeepAnchor, right_abs - left_abs + 1)
            extra.cursor = c
            self.括号高亮列表 = [extra]
            self.高亮当前行()
            self.括号高亮定时器.start(1000)
        except Exception as e:
            logger.error(f"括号匹配异常: {e}")
            self._clear_bracket_highlight()

    def _clear_bracket_highlight(self):
        if self.括号高亮列表 is not None:
            self.括号高亮列表 = None
            self.高亮当前行()

    # ---------- 缩进 ----------
    def indent_selected_lines(self):
        cursor = self.textCursor()
        try:
            if not cursor.hasSelection():
                cursor.beginEditBlock()
                cursor.movePosition(cursor.StartOfBlock)
                cursor.insertText("    ")
                cursor.endEditBlock()
                self.setTextCursor(cursor)
                return
            start = cursor.selectionStart()
            end = cursor.selectionEnd()
            cursor.beginEditBlock()
            block = self.document().findBlock(start)
            while block.isValid() and block.position() <= end:
                cursor.setPosition(block.position())
                cursor.insertText("    ")
                block = block.next()
            cursor.endEditBlock()
        except Exception as e:
            logger.error(f"缩进异常: {e}")

    def unindent_selected_lines(self):
        cursor = self.textCursor()
        try:
            if not cursor.hasSelection():
                block = cursor.block()
                text = block.text()
                cursor.beginEditBlock()
                if text.startswith("    "):
                    cursor.setPosition(block.position())
                    cursor.setPosition(block.position() + 4, cursor.KeepAnchor)
                    cursor.removeSelectedText()
                elif text.startswith("\t"):
                    cursor.setPosition(block.position())
                    cursor.deleteChar()
                cursor.endEditBlock()
                self.setTextCursor(cursor)
                return
            start = cursor.selectionStart()
            end = cursor.selectionEnd()
            cursor.beginEditBlock()
            block = self.document().findBlock(start)
            while block.isValid() and block.position() <= end:
                text = block.text()
                if text.startswith("    "):
                    cursor.setPosition(block.position())
                    cursor.setPosition(block.position() + 4, cursor.KeepAnchor)
                    cursor.removeSelectedText()
                elif text.startswith("\t"):
                    cursor.setPosition(block.position())
                    cursor.deleteChar()
                block = block.next()
            cursor.endEditBlock()
        except Exception as e:
            logger.error(f"取消缩进异常: {e}")

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Tab:
            cursor = self.textCursor()
            cursor.beginEditBlock()
            cursor.insertText("    ")
            cursor.endEditBlock()
            return
        elif event.key() == Qt.Key_Backtab:
            self.unindent_selected_lines()
            return
        elif event.key() == Qt.Key_Backspace:
            cursor = self.textCursor()
            if not cursor.hasSelection():
                pos = cursor.position()
                block = cursor.block()
                col = pos - block.position()
                if col >= 4 and block.text()[col-4:col] == "    ":
                    cursor.beginEditBlock()
                    for _ in range(4):
                        cursor.deletePreviousChar()
                    cursor.endEditBlock()
                    return
            super().keyPressEvent(event)
            return
        elif event.key() in (Qt.Key_Return, Qt.Key_Enter):
            cursor = self.textCursor()
            block = cursor.block()
            text_block = block.text()
            indent = ''
            for ch in text_block:
                if ch in (' ', '\t'):
                    indent += ch
                else:
                    break
            stripped = text_block.rstrip()
            if stripped.endswith(':'):
                indent += '    '
            cursor.insertText('\n' + indent)
            self.setTextCursor(cursor)
            return
        # 检测输入的是否是括号字符
        if event.text() in '()[]{}':
            self.括号输入标志 = True
        super().keyPressEvent(event)

    def contextMenuEvent(self, event):
        菜单 = QMenu(self)
        菜单.setFont(QFont("Microsoft YaHei", 9))

        undo_action = QAction("撤销(&U)", self)
        undo_action.setShortcut(QKeySequence.Undo)
        undo_action.setEnabled(self.document().isUndoAvailable())
        undo_action.triggered.connect(self.undo)
        菜单.addAction(undo_action)

        redo_action = QAction("重做(&R)", self)
        redo_action.setShortcut(QKeySequence.Redo)
        redo_action.setEnabled(self.document().isRedoAvailable())
        redo_action.triggered.connect(self.redo)
        菜单.addAction(redo_action)

        菜单.addSeparator()

        cut_action = QAction("剪切(&X)", self)
        cut_action.setShortcut(QKeySequence.Cut)
        cut_action.setEnabled(self.textCursor().hasSelection())
        cut_action.triggered.connect(self.cut)
        菜单.addAction(cut_action)

        copy_action = QAction("复制(&C)", self)
        copy_action.setShortcut(QKeySequence.Copy)
        copy_action.setEnabled(self.textCursor().hasSelection())
        copy_action.triggered.connect(self.copy)
        菜单.addAction(copy_action)

        paste_action = QAction("粘贴(&V)", self)
        paste_action.setShortcut(QKeySequence.Paste)
        paste_action.setEnabled(bool(QApplication.clipboard().text()))
        paste_action.triggered.connect(self.paste)
        菜单.addAction(paste_action)

        delete_action = QAction("删除(&D)", self)
        delete_action.setShortcut(QKeySequence.Delete)
        delete_action.setEnabled(self.textCursor().hasSelection())
        delete_action.triggered.connect(lambda: self.textCursor().removeSelectedText())
        菜单.addAction(delete_action)

        菜单.addSeparator()

        indent_action = QAction("缩进", self)
        indent_action.setShortcut(QKeySequence("Tab"))
        indent_action.triggered.connect(self.indent_selected_lines)
        菜单.addAction(indent_action)

        unindent_action = QAction("取消缩进", self)
        unindent_action.setShortcut(QKeySequence("Shift+Tab"))
        unindent_action.triggered.connect(self.unindent_selected_lines)
        菜单.addAction(unindent_action)

        菜单.addSeparator()

        select_all_action = QAction("全选(&A)", self)
        select_all_action.setShortcut(QKeySequence.SelectAll)
        select_all_action.triggered.connect(self.selectAll)
        菜单.addAction(select_all_action)

        菜单.exec_(event.globalPos())
