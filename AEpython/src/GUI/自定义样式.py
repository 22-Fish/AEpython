# 自定义样式.py - 彻底修复崩溃、语法错误与容器可见性，保留字号配置
import os, json, logging
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QListWidget, QTextEdit,
    QPushButton, QLabel, QWidget, QTabWidget,
    QColorDialog, QCheckBox, QMessageBox
)
from PyQt5.QtGui import QFont, QColor, QFontDatabase, QTextCursor
from PyQt5.QtCore import Qt, QTimer
from config.路径 import 获取配置目录
from GUI.语法高亮器 import Python语法高亮器

logger = logging.getLogger('样式')
logger.setLevel(logging.DEBUG)
if not logger.handlers:
    handler = logging.StreamHandler()
    handler.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s: [样式] %(message)s'))
    logger.addHandler(handler)

class 样式设置对话框(QDialog):
    def __init__(self, 当前字体, 当前颜色配置, 父窗口=None):
        super().__init__(父窗口)
        self.setWindowTitle("自定义样式")
        self.resize(950, 650)
        self.当前字体 = 当前字体
        # 未读取到配置文件时，提供默认颜色配置
        self.颜色配置 = 当前颜色配置.copy() if 当前颜色配置 else self._默认配置()
        self.当前选中类型 = "普通代码"
        self.示例高亮器 = None
        self.示例代码 = (
            "class 类:\n"
            "    @装饰器\n"
            "    def 函数():\n"
            "        try:\n"
            "            #这是注释\n"
            "            print(\"字符串\")\n"
            "        except Exception:\n"
            "            return 1 + 2"
        )
        self._创建界面()
        self._加载字体列表()
        self._显示示例代码()
        self._更新颜色面板()

    def _默认配置(self):
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
            "选中区": {"文字色": "#ffffff", "背景色": "#3399ff"},
            "默认背景色": "#ffffff",
            "当前行背景色": "#ffff99",
            "行号": {"文字色": "#000000", "背景色": "#e0e0e0"},
            "光标": {"文字色": "#000000"},
            # 输出框的颜色（与代码编辑框区分开）
            "解释器输出": {"文字色": "#C62828"},
            "系统输出消息": {"文字色": "#2E7D32"}
        }

    @staticmethod
    def _运行类默认色(类型名):
        """只有文字色的类型（光标 / 解释器输出 / 系统输出消息）在配置缺失时的默认色"""
        return {
            "光标": "#000000",
            "解释器输出": "#C62828",
            "系统输出消息": "#2E7D32",
        }.get(类型名, "#000000")

    def _创建界面(self):
        主布局 = QVBoxLayout(self)
        self.标签页 = QTabWidget()
        主布局.addWidget(self.标签页)

        # ---------- 字体设置页 ----------
        字体页 = QWidget()
        字体布局 = QHBoxLayout(字体页)
        self.字体列表控件 = QListWidget()
        字体布局.addWidget(self.字体列表控件)
        右字体区 = QVBoxLayout()
        字体预览标签 = QLabel("预览效果：")
        字体预览标签.setFont(QFont("Microsoft YaHei", 10))
        右字体区.addWidget(字体预览标签)
        self.字体预览区 = QTextEdit()
        self.字体预览区.setReadOnly(True)
        self.字体预览区.setPlainText(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n1234567890\n你好，世界！Hello World!"
        )
        右字体区.addWidget(self.字体预览区)
        字体布局.addLayout(右字体区)
        self.标签页.addTab(字体页, "字体")

        # ---------- 颜色设置页 ----------
        颜色页 = QWidget()
        颜色水平布局 = QHBoxLayout(颜色页)

        self.类型列表 = QListWidget()
        self.类型列表.addItems([
            "关键字", "装饰器", "字符串", "注释", "普通代码",
            "异常类型", "值", "运算符", "选中区", "括号匹配",
            "光标", "行号", "解释器输出", "系统输出消息"
        ])
        self.类型列表.setCurrentRow(4)
        self.类型列表.currentItemChanged.connect(lambda current, previous: self._类型切换(current))
        颜色水平布局.addWidget(self.类型列表, 1)

        左颜色区 = QVBoxLayout()
        左颜色区.setAlignment(Qt.AlignTop)

        self.类型标签 = QLabel("当前类型：普通代码")
        self.类型标签.setFont(QFont("Microsoft YaHei", 10))
        左颜色区.addWidget(self.类型标签)

        # --- 文字色容器（用 QWidget 包裹，方便控制可见性）---
        self.文字色容器 = QWidget()
        文字色容器布局 = QHBoxLayout(self.文字色容器)
        文字色容器布局.setContentsMargins(0, 0, 0, 0)
        文字色容器布局.addWidget(QLabel("文字色："))
        self.文字色按钮 = QPushButton()
        self.文字色按钮.setFixedSize(40, 20)
        self.文字色按钮.clicked.connect(lambda: self._选择颜色("文字色"))
        文字色容器布局.addWidget(self.文字色按钮)
        左颜色区.addWidget(self.文字色容器)

        # --- 背景色布局（无需容器，但也要保证能控制可见按钮，这里直接保留）---
        背景色布局 = QHBoxLayout()
        背景色布局.addWidget(QLabel("背景色："))
        self.背景色按钮 = QPushButton()
        self.背景色按钮.setFixedSize(40, 20)
        self.背景色按钮.clicked.connect(lambda: self._选择颜色("背景色"))
        背景色布局.addWidget(self.背景色按钮)
        左颜色区.addLayout(背景色布局)

        self.使用背景色复选框 = QCheckBox("使用自定义背景色")
        self.使用背景色复选框.toggled.connect(lambda state: self._切换背景色启用(state))
        左颜色区.addWidget(self.使用背景色复选框)

        self.自定义文字色复选框 = QCheckBox("自定义文字色")
        self.自定义文字色复选框.toggled.connect(lambda state: self._切换选中区文字色启用(state))
        左颜色区.addWidget(self.自定义文字色复选框)

        左颜色区.addSpacing(15)
        左颜色区.addWidget(QLabel("全局颜色设置"))

        默认背景布局 = QHBoxLayout()
        默认背景布局.addWidget(QLabel("默认背景："))
        self.默认背景色按钮 = QPushButton()
        self.默认背景色按钮.setFixedSize(40, 20)
        self.默认背景色按钮.clicked.connect(lambda: self._选择颜色("默认背景色"))
        默认背景布局.addWidget(self.默认背景色按钮)
        左颜色区.addLayout(默认背景布局)

        当前行布局 = QHBoxLayout()
        当前行布局.addWidget(QLabel("当前行背景："))
        self.当前行颜色按钮 = QPushButton()
        self.当前行颜色按钮.setFixedSize(40, 20)
        self.当前行颜色按钮.clicked.connect(lambda: self._选择颜色("当前行背景色"))
        当前行布局.addWidget(self.当前行颜色按钮)
        左颜色区.addLayout(当前行布局)

        颜色水平布局.addLayout(左颜色区, 2)

        右颜色区 = QVBoxLayout()
        提示标签 = QLabel("语法高亮预览：")
        提示标签.setFont(QFont("Microsoft YaHei", 9))
        右颜色区.addWidget(提示标签)
        self.示例编辑区 = QTextEdit()
        self.示例编辑区.setReadOnly(True)
        self.示例编辑区.setFont(QFont("Consolas", 11))
        默认背景 = self.颜色配置.get("默认背景色", "#ffffff")
        self.示例编辑区.setStyleSheet(f"QTextEdit {{ background-color: {默认背景}; }}")
        右颜色区.addWidget(self.示例编辑区)
        颜色水平布局.addLayout(右颜色区, 3)

        self.标签页.addTab(颜色页, "语法")

        按钮布局 = QHBoxLayout()
        保存按钮 = QPushButton("保存")
        保存按钮.clicked.connect(lambda: self.保存设置())
        取消按钮 = QPushButton("取消")
        取消按钮.clicked.connect(self.reject)
        按钮布局.addStretch()
        按钮布局.addWidget(保存按钮)
        按钮布局.addWidget(取消按钮)
        主布局.addLayout(按钮布局)

        self.字体列表控件.currentTextChanged.connect(lambda 字体名: self.预览字体(字体名))

    def _类型切换(self, current):
        if current is None:
            return
        self.当前选中类型 = current.text()
        QTimer.singleShot(0,lambda: self._安全更新面板())

    def _安全更新面板(self):
        """类型切换后安全更新 UI 状态"""
        try:
            t = self.当前选中类型
            # 控制复选框可见性
            if t == "选中区":
                self.使用背景色复选框.setVisible(False)
                self.自定义文字色复选框.setVisible(True)
                启用文字色 = self.颜色配置.get("选中区", {}).get("启用自定义文字色", False)
                self.自定义文字色复选框.blockSignals(True)
                self.自定义文字色复选框.setChecked(启用文字色)
                self.自定义文字色复选框.blockSignals(False)
                self.文字色按钮.setEnabled(启用文字色)
                self.背景色按钮.setEnabled(True)
            elif t in ("括号匹配", "行号", "光标", "解释器输出", "系统输出消息"):
                self.使用背景色复选框.setVisible(False)
                self.自定义文字色复选框.setVisible(False)
                self.背景色按钮.setEnabled(True)
            else:
                self.使用背景色复选框.setVisible(True)
                self.自定义文字色复选框.setVisible(False)
            # 控制文字色容器可见性
            if t == "括号匹配":
                self.文字色容器.setVisible(False)      # 括号匹配只需背景色
            elif t in ("光标", "解释器输出", "系统输出消息"):
                self.文字色容器.setVisible(True)       # 这三项只需文字色
                self.背景色按钮.setEnabled(False)      # 没有背景色
            else:
                self.文字色容器.setVisible(True)
            self._更新颜色面板()
        except Exception:
            logger.error("安全更新面板异常", exc_info=True)

    def _更新颜色面板(self):
        try:
            t = self.当前选中类型
            self.类型标签.setText(f"当前类型：{t}")

            if t == "括号匹配":
                背景色 = self.颜色配置.get("括号匹配", {}).get("背景色", "#808080")
                self.背景色按钮.setStyleSheet(f"background-color: {背景色}; border: 1px solid #333;")
            elif t == "行号":
                文字色 = self.颜色配置.get("行号", {}).get("文字色", "#000000")
                背景色 = self.颜色配置.get("行号", {}).get("背景色", "#e0e0e0")
                self.文字色按钮.setStyleSheet(f"background-color: {文字色}; border: 1px solid #333;")
                self.背景色按钮.setStyleSheet(f"background-color: {背景色}; border: 1px solid #333;")
            elif t in ("光标", "解释器输出", "系统输出消息"):
                文字色 = self.颜色配置.get(t, {}).get("文字色", self._运行类默认色(t))
                self.文字色按钮.setStyleSheet(f"background-color: {文字色}; border: 1px solid #333;")
                self.背景色按钮.setStyleSheet("background-color: transparent; border: 1px solid #aaa;")
            elif t == "选中区":
                文字色 = self.颜色配置.get("选中区", {}).get("文字色", "#ffffff")
                背景色 = self.颜色配置.get("选中区", {}).get("背景色", "#3399ff")
                启用文字色 = self.颜色配置.get("选中区", {}).get("启用自定义文字色", False)
                self.文字色按钮.setStyleSheet(f"background-color: {文字色}; border: 1px solid #333;")
                self.背景色按钮.setStyleSheet(f"background-color: {背景色}; border: 1px solid #333;")
                self.自定义文字色复选框.blockSignals(True)
                self.自定义文字色复选框.setChecked(启用文字色)
                self.自定义文字色复选框.blockSignals(False)
                self.文字色按钮.setEnabled(启用文字色)
            else:
                类型配置 = self.颜色配置.get(t, {})
                文字色 = 类型配置.get("文字色", "#ececec")
                self.文字色按钮.setStyleSheet(f"background-color: {文字色}; border: 1px solid #333;")

                背景色 = 类型配置.get("背景色", "")
                使用背景 = bool(背景色)
                self.使用背景色复选框.blockSignals(True)
                self.使用背景色复选框.setChecked(使用背景)
                self.使用背景色复选框.blockSignals(False)
                if 使用背景:
                    self.背景色按钮.setStyleSheet(f"background-color: {背景色}; border: 1px solid #333;")
                    self.背景色按钮.setEnabled(True)
                else:
                    self.背景色按钮.setStyleSheet("background-color: transparent; border: 1px solid #aaa;")
                    self.背景色按钮.setEnabled(False)

            默认背景色 = self.颜色配置.get("默认背景色", "#ffffff")
            self.默认背景色按钮.setStyleSheet(f"background-color: {默认背景色}; border: 1px solid #333;")
            self.示例编辑区.setStyleSheet(f"QTextEdit {{ background-color: {默认背景色}; }}")
            当前行色 = self.颜色配置.get("当前行背景色", "#ffff99")
            self.当前行颜色按钮.setStyleSheet(f"background-color: {当前行色}; border: 1px solid #333;")
        except Exception:
            logger.error("更新颜色面板异常", exc_info=True)

    def _切换选中区文字色启用(self, state):
        选中区配置 = self.颜色配置.setdefault("选中区", {})
        选中区配置["启用自定义文字色"] = state
        self.文字色按钮.setEnabled(state)
        if state and not 选中区配置.get("文字色"):
            选中区配置["文字色"] = "#ffffff"
        self._更新颜色面板()

    def _切换背景色启用(self, state):
        if self.当前选中类型 in ("选中区", "括号匹配", "行号", "光标", "解释器输出", "系统输出消息"):
            return
        try:
            类型配置 = self.颜色配置.setdefault(self.当前选中类型, {})
            if state:
                if not 类型配置.get("背景色"):
                    类型配置["背景色"] = self.颜色配置.get("默认背景色", "#ffffff")
                self.背景色按钮.setEnabled(True)
                self.背景色按钮.setStyleSheet(f"background-color: {类型配置['背景色']}; border: 1px solid #333;")
            else:
                类型配置["背景色"] = ""
                self.背景色按钮.setStyleSheet("background-color: transparent; border: 1px solid #aaa;")
                self.背景色按钮.setEnabled(False)
        except Exception:
            logger.error("切换背景色异常", exc_info=True)

    def _选择颜色(self, 目标):
        try:
            if self.当前选中类型 == "括号匹配":
                if 目标 != "背景色":
                    return
                初始 = QColor(self.颜色配置.get("括号匹配", {}).get("背景色", "#808080"))
                颜色 = self._获取颜色(初始, "括号匹配背景色")
                if 颜色.isValid():
                    self.颜色配置.setdefault("括号匹配", {})["背景色"] = 颜色.name()
                    self._更新颜色面板()
                    self._显示示例代码()
                return
            elif self.当前选中类型 == "行号":
                if 目标 == "文字色":
                    初始 = QColor(self.颜色配置.get("行号", {}).get("文字色", "#000000"))
                elif 目标 == "背景色":
                    初始 = QColor(self.颜色配置.get("行号", {}).get("背景色", "#e0e0e0"))
                else:
                    return
                颜色 = self._获取颜色(初始, f"行号{目标}")
                if 颜色.isValid():
                    self.颜色配置.setdefault("行号", {})[目标] = 颜色.name()
                    self._更新颜色面板()
                    self._显示示例代码()
                return
            elif self.当前选中类型 in ("光标", "解释器输出", "系统输出消息"):
                当前类型 = self.当前选中类型
                if 目标 != "文字色":
                    return
                初始 = QColor(self.颜色配置.get(当前类型, {}).get("文字色", self._运行类默认色(当前类型)))
                颜色 = self._获取颜色(初始, f"{当前类型}文字色")
                if 颜色.isValid():
                    self.颜色配置.setdefault(当前类型, {})["文字色"] = 颜色.name()
                    self._更新颜色面板()
                    self._显示示例代码()
                return
            elif self.当前选中类型 == "选中区":
                if 目标 == "文字色":
                    初始 = QColor(self.颜色配置.get("选中区", {}).get("文字色", "#ffffff"))
                elif 目标 == "背景色":
                    初始 = QColor(self.颜色配置.get("选中区", {}).get("背景色", "#3399ff"))
                else:
                    return
                颜色 = self._获取颜色(初始, f"选中区{目标}")
                if 颜色.isValid():
                    self.颜色配置.setdefault("选中区", {})[目标] = 颜色.name()
                    self._更新颜色面板()
                    self._显示示例代码()
                return
            else:
                if 目标 == "默认背景色":
                    初始 = QColor(self.颜色配置.get("默认背景色", "#ffffff"))
                    颜色 = self._获取颜色(初始, "默认背景色")
                    if 颜色.isValid():
                        self.颜色配置["默认背景色"] = 颜色.name()
                elif 目标 == "当前行背景色":
                    初始 = QColor(self.颜色配置.get("当前行背景色", "#ffff99"))
                    颜色 = self._获取颜色(初始, "当前行背景色")
                    if 颜色.isValid():
                        self.颜色配置["当前行背景色"] = 颜色.name()
                elif 目标 == "文字色":
                    类型配置 = self.颜色配置.setdefault(self.当前选中类型, {})
                    初始 = QColor(类型配置.get("文字色", "black"))
                    颜色 = self._获取颜色(初始, f"{self.当前选中类型}文字色")
                    if 颜色.isValid():
                        类型配置["文字色"] = 颜色.name()
                elif 目标 == "背景色":
                    类型配置 = self.颜色配置.setdefault(self.当前选中类型, {})
                    初始 = QColor(类型配置.get("背景色", self.颜色配置.get("默认背景色", "#ffffff")))
                    颜色 = self._获取颜色(初始, f"{self.当前选中类型}背景色")
                    if 颜色.isValid():
                        类型配置["背景色"] = 颜色.name()
                else:
                    return
                if 颜色.isValid():
                    self._更新颜色面板()
                    self._显示示例代码()
        except Exception:
            logger.error("选择颜色异常", exc_info=True)

    def _显示示例代码(self):
        try:
            if self.示例高亮器:
                self.示例高亮器.setDocument(None)
                self.示例高亮器.deleteLater()
                self.示例高亮器 = None
            self.示例编辑区.clear()
            self.示例编辑区.setPlainText(self.示例代码)
            self.示例高亮器 = Python语法高亮器(self.示例编辑区.document(), self.颜色配置)
            # 选中示例代码一部分来展示选中区颜色
            self.示例编辑区.setReadOnly(False)
            text_len = len(self.示例代码)
            if text_len > 20:
                cursor = self.示例编辑区.textCursor()
                cursor.setPosition(10)
                cursor.movePosition(QTextCursor.Right, QTextCursor.KeepAnchor, 15)
                self.示例编辑区.setTextCursor(cursor)
            self.示例编辑区.setReadOnly(True)
        except Exception:
            logger.error("显示示例代码异常", exc_info=True)

    def _加载字体列表(self):
        try:
            db = QFontDatabase()
            for family in db.families():
                self.字体列表控件.addItem(family)
            items = self.字体列表控件.findItems(self.当前字体, Qt.MatchExactly)
            if items:
                self.字体列表控件.setCurrentItem(items[0])
                self.预览字体(self.当前字体)
        except Exception:
            logger.error("加载字体列表异常", exc_info=True)

    def 预览字体(self, 字体名):
        try:
            字体 = QFont()
            字体.setFamilies([字体名, "Microsoft YaHei"])
            字体.setPointSize(12)
            self.字体预览区.setFont(字体)
            self.当前字体 = 字体名
        except Exception:
            logger.error("预览字体异常", exc_info=True)

    def 保存设置(self):
        try:
            配置目录 = 获取配置目录()
            os.makedirs(配置目录, exist_ok=True)

            # 读取现有 font.json 内容，保留 font_size 字段
            font_config_path = os.path.join(配置目录, "font.json")
            existing_config = {}
            if os.path.exists(font_config_path):
                try:
                    with open(font_config_path, "r", encoding="utf-8") as f:
                        existing_config = json.load(f)
                except:
                    pass

            # 更新字体族，保留字号
            existing_config["font"] = self.当前字体
            # 从父窗口编辑器获取当前实际字号（用户可能用 Ctrl+滚轮调整过）
            # 覆盖旧配置，确保不会回退到旧字号
            主窗口 = self.parent()
            if 主窗口 and hasattr(主窗口, '编辑器'):
                existing_config["font_size"] = 主窗口.编辑器.当前字号
                logger.debug(f"[调试] 样式保存: 从编辑器获取当前字号={主窗口.编辑器.当前字号}")
            else:
                existing_config["font_size"] = existing_config.get("font_size", 12)
                logger.debug(f"[调试] 样式保存: 无法获取编辑器字号，使用旧配置={existing_config['font_size']}")

            logger.debug(f"[调试] 样式保存: 即将写入 font.json => font={existing_config['font']}, font_size={existing_config['font_size']}")
            with open(font_config_path, "w", encoding="utf-8") as f:
                json.dump(existing_config, f, indent=2, ensure_ascii=False)

            # 保存颜色配置
            with open(os.path.join(配置目录, "colors.json"), "w", encoding="utf-8") as f:
                json.dump(self.颜色配置, f, indent=2)

            # 通知主窗口编辑器重新加载字体
            主窗口 = self.parent()
            if 主窗口 and hasattr(主窗口, '编辑器'):
                logger.debug(f"[调试] 样式保存: 通知编辑器重新加载字体配置")
                主窗口.编辑器.重新加载字体配置()

            self.accept()
        except Exception as e:
            logger.error("保存失败", exc_info=True)
            QMessageBox.critical(self, "保存失败", str(e))

    def _获取颜色(self, 初始颜色, 标题="选择颜色"):
        dlg = QColorDialog(初始颜色, self)
        dlg.setWindowTitle(标题)
        dlg.setOptions(QColorDialog.DontUseNativeDialog)
        for btn in dlg.findChildren(QPushButton):
            if btn.text() == "OK":
                btn.setText("确定")
            elif btn.text() == "Cancel":
                btn.setText("取消")
        if dlg.exec_() == QColorDialog.Accepted:
            return dlg.currentColor()
        return QColor()


