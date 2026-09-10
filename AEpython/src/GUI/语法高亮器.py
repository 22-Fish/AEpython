# 语法高亮器.py - 支持三引号跨行字符串、状态机
# UTF-16 对齐修复可开关，启用时精确匹配 Qt 的代码单元计数以修正 emoji 偏移
import re
from PyQt5.QtGui import QColor, QTextCharFormat, QSyntaxHighlighter


# 多行状态常量
状态_普通 = 0
状态_三双引字符串 = 1
状态_三单引字符串 = 2


class Python语法高亮器(QSyntaxHighlighter):
    def __init__(self, 父对象=None, 颜色配置=None, utf16对齐修复=False):
        super().__init__(父对象)
        if 颜色配置 is None:
            颜色配置 = self.默认颜色配置()
        self.颜色配置 = 颜色配置
        self.默认背景色 = self.颜色配置.get("默认背景色", "#ffffff")
        self.utf16对齐修复 = utf16对齐修复

        self.关键字格式 = self._创建格式(颜色配置.get("关键字", {}).get("文字色", "orange"), 颜色配置.get("关键字", {}).get("背景色", ""), "关键字")
        self.装饰器格式 = self._创建格式(颜色配置.get("装饰器", {}).get("文字色", "purple"), 颜色配置.get("装饰器", {}).get("背景色", ""), "装饰器")
        self.字符串格式 = self._创建格式(颜色配置.get("字符串", {}).get("文字色", "green"), 颜色配置.get("字符串", {}).get("背景色", ""), "字符串")
        self.注释格式 = self._创建格式(颜色配置.get("注释", {}).get("文字色", "gray"), 颜色配置.get("注释", {}).get("背景色", ""), "注释")
        self.普通代码格式 = self._创建格式(颜色配置.get("普通代码", {}).get("文字色", "black"), 颜色配置.get("普通代码", {}).get("背景色", ""), "普通代码")
        self.异常类型格式 = self._创建格式(颜色配置.get("异常类型", {}).get("文字色", "#cc0000"), 颜色配置.get("异常类型", {}).get("背景色", ""), "异常类型")
        self.值格式 = self._创建格式(颜色配置.get("值", {}).get("文字色", "darkorange"), 颜色配置.get("值", {}).get("背景色", ""), "值")
        self.运算符格式 = self._创建格式(颜色配置.get("运算符", {}).get("文字色", "blue"), 颜色配置.get("运算符", {}).get("背景色", ""), "运算符")

        关键字列表 = ["def", "class", "if", "else", "elif", "while", "for", "in",
            "return", "import", "from", "as", "try", "except", "finally",
            "with", "pass", "break", "continue", "and", "or", "not", "is",
            "None", "True", "False", "lambda", "yield", "global", "nonlocal",
            "assert", "raise", "del"]
        self.关键字规则 = [re.compile(r'\b' + w + r'\b') for w in 关键字列表]

        异常列表 = ["Exception", "ValueError", "TypeError", "KeyError", "IndexError",
            "AttributeError", "ImportError", "OSError", "RuntimeError",
            "StopIteration", "GeneratorExit", "KeyboardInterrupt", "SystemExit",
            "BaseException", "ArithmeticError", "LookupError", "AssertionError",
            "MemoryError", "NameError", "NotImplementedError", "SyntaxError",
            "IndentationError", "TabError", "SystemError", "UnicodeError",
            "UnicodeEncodeError", "UnicodeDecodeError", "UnicodeTranslateError",
            "Warning", "DeprecationWarning", "PendingDeprecationWarning",
            "FutureWarning", "ImportWarning", "UnicodeWarning", "BytesWarning",
            "ResourceWarning"]
        self.异常规则 = [re.compile(r'\b' + w + r'\b') for w in 异常列表]

        self.装饰器规则 = re.compile(r'@[\w.]+')
        self.运算符规则 = re.compile(r'[+\-*/|]')
        self.数字规则 = re.compile(r'\b\d+(\.\d+)?\b')

    @staticmethod
    def 默认颜色配置():
        return {
            "关键字": {"文字色": "orange", "背景色": ""},
            "装饰器": {"文字色": "purple", "背景色": ""},
            "字符串": {"文字色": "green", "背景色": ""},
            "注释": {"文字色": "gray", "背景色": ""},
            "普通代码": {"文字色": "black", "背景色": ""},
            "异常类型": {"文字色": "#cc0000", "背景色": ""},
            "值": {"文字色": "darkorange", "背景色": ""},
            "运算符": {"文字色": "blue", "背景色": ""},
            "括号匹配": {"背景色": "#808080"},
            "选中区": {"文字色": "#ffffff", "背景色": "#3399ff", "启用自定义文字色": False},
            "默认背景色": "#ffffff",
            "当前行背景色": "#ffff99",
            "行号": {"文字色": "#000000", "背景色": "#e0e0e0"},
            "光标": {"文字色": "#000000"},
            # 输出框：解释器的错误/日志输出（默认红）、AEpy 自己的消息（默认绿）。
            # 程序自身的 stdout 跟随“普通代码”文字色。
            "解释器输出": {"文字色": "#C62828"},
            "系统输出消息": {"文字色": "#2E7D32"}
        }

    def _创建格式(self, 文字色, 背景色, 类型名):
        格式 = QTextCharFormat()
        if 文字色:
            格式.setForeground(QColor(文字色))
        if 背景色:
            格式.setBackground(QColor(背景色))
        格式.setToolTip(类型名)
        return 格式

    @staticmethod
    def _构建偏移表(text):
        """
        构建 Python 索引 → Qt 代码单元索引 的偏移表。
        返回 (qt_units, py_to_qt)：
        - qt_units: Qt 代码单元总数
        - py_to_qt: list，py_to_qt[py_idx] = 对应的 qt_idx
        """
        py_len = len(text)
        if py_len == 0:
            return 0, []

        encoded = text.encode('utf-16-le')
        total_qt_units = len(encoded) // 2

        py_to_qt = [0] * (py_len + 1)
        qt_pos = 0
        for i, ch in enumerate(text):
            py_to_qt[i] = qt_pos
            if ord(ch) > 0xFFFF:
                qt_pos += 2
            else:
                qt_pos += 1
        py_to_qt[py_len] = total_qt_units

        return total_qt_units, py_to_qt

    def _获取偏移表(self, text):
        """内部缓存偏移表，避免重复计算。"""
        if self.utf16对齐修复:
            return self._构建偏移表(text)
        return len(text), None

    def _应用格式(self, fmt, text, py_start, py_count, qt_total, py_to_qt):
        """根据是否启用对齐修复选择 Qt setFormat 模式。"""
        if self.utf16对齐修复 and py_to_qt is not None:
            s = py_to_qt[py_start]
            e = py_to_qt[py_start + py_count]
            self.setFormat(s, e - s, fmt)
        else:
            if py_start + py_count > qt_total:
                py_count = qt_total - py_start
            self.setFormat(py_start, py_count, fmt)

    def _区间(self, text, py_s, py_e, qt_total, py_to_qt):
        """返回 (qt_start, qt_end) 或 (py_s, py_e)。"""
        if self.utf16对齐修复 and py_to_qt is not None:
            return (py_to_qt[py_s], py_to_qt[py_e])
        return (py_s, py_e)

    def highlightBlock(self, text):
        py_len = len(text)
        if py_len == 0:
            # 空行：继承上一行的跨行三引号状态，但无需设置格式
            prev_state = self.previousBlockState()
            if prev_state in (状态_三双引字符串, 状态_三单引字符串):
                self.setCurrentBlockState(prev_state)
            else:
                self.setCurrentBlockState(状态_普通)
            return

        qt_total, py_to_qt = self._获取偏移表(text)

        # 整行打底
        def _sf(start, count, fmt):
            if self.utf16对齐修复 and py_to_qt is not None:
                self.setFormat(py_to_qt[start], py_to_qt[start+count] - py_to_qt[start], fmt)
            else:
                self.setFormat(start, count, fmt)
        def _qr(s, e):
            if self.utf16对齐修复 and py_to_qt is not None:
                return (py_to_qt[s], py_to_qt[e])
            return (s, e)

        _sf(0, py_len, self.普通代码格式)

        prev_state = self.previousBlockState()
        i = 0
        in_str = False
        str_start = 0
        str_quote = ''
        已应用区间 = []

        # ---- 跨行三引号继承 ----
        if prev_state == 状态_三双引字符串:
            end = self._find_triple_closing(text, 0, '"""')
            if end is not None:
                _sf(0, end + 3, self.字符串格式)
                已应用区间.append(_qr(0, end + 3))
                i = end + 3
            else:
                _sf(0, py_len, self.字符串格式)
                self.setCurrentBlockState(状态_三双引字符串)
                return
        elif prev_state == 状态_三单引字符串:
            end = self._find_triple_closing(text, 0, "'''")
            if end is not None:
                _sf(0, end + 3, self.字符串格式)
                已应用区间.append(_qr(0, end + 3))
                i = end + 3
            else:
                _sf(0, py_len, self.字符串格式)
                self.setCurrentBlockState(状态_三单引字符串)
                return

        # ---- 逐字符扫描 ----
        while i < py_len:
            ch = text[i]

            # 单行字符串内部
            if in_str:
                if ch == '\\' and i + 1 < py_len:
                    i += 2
                    continue
                if ch == str_quote:
                    _sf(str_start, i - str_start + 1, self.字符串格式)
                    已应用区间.append(_qr(str_start, i + 1))
                    in_str = False
                i += 1
                continue

            # 只有不在字符串内时才检查下面这些

            # 三引号 """ — 先检查，避免和单引号冲突
            if ch == '"' and i + 2 < py_len and text[i:i+3] == '"""':
                end = self._find_triple_closing(text, i + 3, '"""')
                if end is not None:
                    _sf(i, end + 3 - i, self.字符串格式)
                    已应用区间.append(_qr(i, end + 3))
                    i = end + 3
                else:
                    _sf(i, py_len - i, self.字符串格式)
                    已应用区间.append(_qr(i, py_len))
                    self.setCurrentBlockState(状态_三双引字符串)
                    i = py_len
                continue

            # 三引号 '''
            if ch == "'" and i + 2 < py_len and text[i:i+3] == "'''":
                end = self._find_triple_closing(text, i + 3, "'''")
                if end is not None:
                    _sf(i, end + 3 - i, self.字符串格式)
                    已应用区间.append(_qr(i, end + 3))
                    i = end + 3
                else:
                    _sf(i, py_len - i, self.字符串格式)
                    已应用区间.append(_qr(i, py_len))
                    self.setCurrentBlockState(状态_三单引字符串)
                    i = py_len
                continue

            # 注释 — 之后的内容全部是注释
            if ch == '#':
                _sf(i, py_len - i, self.注释格式)
                已应用区间.append(_qr(i, py_len))
                break

            # 单行字符串 "
            if ch == '"':
                str_start = i
                str_quote = '"'
                in_str = True
                i += 1
                continue

            # 单行字符串 '
            if ch == "'":
                str_start = i
                str_quote = "'"
                in_str = True
                i += 1
                continue

            i += 1

        # ---- 未闭合的单行字符串 ----
        if in_str:
            _sf(str_start, py_len - str_start, self.字符串格式)
            已应用区间.append(_qr(str_start, py_len))

        if self.currentBlockState() == -1:
            self.setCurrentBlockState(状态_普通)

        self._应用普通规则(text, py_to_qt, 已应用区间)

    def _find_triple_closing(self, text, start, delim):
        """从 start 开始找下一个非转义的三引号定界符"""
        i = start
        l = len(text)
        while i < l:
            if text[i] == '\\' and i + 1 < l:
                i += 2
                continue
            if i + 2 < l and text[i:i+3] == delim:
                return i
            i += 1
        return None

    def _应用普通规则(self, text, py_to_qt, 已应用区间):
        py_len = len(text)

        if not 已应用区间:
            self._全部应用(text, py_to_qt)
            return

        # 取边界：如果启用对齐修复用 Qt 坐标，否则用 Python 坐标
        def 取边界():
            if self.utf16对齐修复 and py_to_qt is not None:
                # Qt 坐标 → Python 索引（二分查找）
                def to_py(qt_pos):
                    lo, hi = 0, py_len
                    while lo < hi:
                        mid = (lo + hi) // 2
                        if py_to_qt[mid] < qt_pos:
                            lo = mid + 1
                        else:
                            hi = mid
                    return lo
                return to_py, py_len
            else:
                return (lambda x: x), py_len

        to_py, total = 取边界()

        分段 = []
        当前起点 = 0
        for s, e in sorted(已应用区间):
            py_s = to_py(s) if callable(to_py) else s
            py_e = to_py(e) if callable(to_py) else e
            if 当前起点 < py_s:
                分段.append((当前起点, py_s))
            当前起点 = py_e
        if 当前起点 < total:
            分段.append((当前起点, total))

        for seg_s, seg_e in 分段:
            if seg_s >= seg_e:
                continue
            seg_text = text[seg_s:seg_e]

            for m in self.装饰器规则.finditer(seg_text):
                s, e = seg_s + m.start(), seg_s + m.end()
                self._应用格式(self.装饰器格式, text, s, e - s, py_len, py_to_qt)

            for 规则 in self.关键字规则:
                for m in 规则.finditer(seg_text):
                    s, e = seg_s + m.start(), seg_s + m.end()
                    self._应用格式(self.关键字格式, text, s, e - s, py_len, py_to_qt)

            for 规则 in self.异常规则:
                for m in 规则.finditer(seg_text):
                    s, e = seg_s + m.start(), seg_s + m.end()
                    self._应用格式(self.异常类型格式, text, s, e - s, py_len, py_to_qt)

            for m in self.数字规则.finditer(seg_text):
                s, e = seg_s + m.start(), seg_s + m.end()
                self._应用格式(self.值格式, text, s, e - s, py_len, py_to_qt)

            for m in self.运算符规则.finditer(seg_text):
                s, e = seg_s + m.start(), seg_s + m.end()
                self._应用格式(self.运算符格式, text, s, e - s, py_len, py_to_qt)

    def _全部应用(self, text, py_to_qt):
        py_len = len(text)
        for m in self.装饰器规则.finditer(text):
            s, e = m.start(), m.end()
            self._应用格式(self.装饰器格式, text, s, e - s, py_len, py_to_qt)
        for 规则 in self.关键字规则:
            for m in 规则.finditer(text):
                s, e = m.start(), m.end()
                self._应用格式(self.关键字格式, text, s, e - s, py_len, py_to_qt)
        for 规则 in self.异常规则:
            for m in 规则.finditer(text):
                s, e = m.start(), m.end()
                self._应用格式(self.异常类型格式, text, s, e - s, py_len, py_to_qt)
        for m in self.数字规则.finditer(text):
            s, e = m.start(), m.end()
            self._应用格式(self.值格式, text, s, e - s, py_len, py_to_qt)
        for m in self.运算符规则.finditer(text):
            s, e = m.start(), m.end()
            self._应用格式(self.运算符格式, text, s, e - s, py_len, py_to_qt)

    def 重新应用配置(self, 新配置):
        self.颜色配置 = 新配置
        self.默认背景色 = self.颜色配置.get("默认背景色", "#ffffff")
        for 格式, key in [
            (self.关键字格式, "关键字"), (self.装饰器格式, "装饰器"),
            (self.字符串格式, "字符串"), (self.注释格式, "注释"),
            (self.普通代码格式, "普通代码"), (self.异常类型格式, "异常类型"),
            (self.值格式, "值"), (self.运算符格式, "运算符")
        ]:
            self._更新格式颜色(格式, 新配置.get(key, {}).get("文字色", ""),
                              新配置.get(key, {}).get("背景色", ""))
        self.rehighlight()

    def _更新格式颜色(self, 格式, 文字色, 背景色):
        if 文字色:
            格式.setForeground(QColor(文字色))
        if 背景色:
            格式.setBackground(QColor(背景色))
        else:
            格式.clearBackground()
