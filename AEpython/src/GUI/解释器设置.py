# 解释器设置.py - 解释器设置对话框（含“使用虚拟环境”复选框）
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton,
    QFileDialog, QGroupBox, QCheckBox
)

from config.解释器配置 import 读取解释器设置, 保存解释器设置
from config.项目入口 import 项目解释器信息
from 解释器.虚拟环境 import 查找虚拟环境, 取解释器


class 解释器设置对话框(QDialog):
    def __init__(self, 父窗口=None, 项目目录=None):
        super().__init__(父窗口)
        self.setWindowTitle("解释器设置")
        self.resize(620, 470)
        self.项目目录 = 项目目录

        当前设置 = 读取解释器设置()

        主布局 = QVBoxLayout(self)
        主布局.setSpacing(12)

        # ---------- 虚拟环境 ----------
        虚拟环境组 = QGroupBox("虚拟环境")
        虚拟环境布局 = QVBoxLayout(虚拟环境组)

        self.虚拟环境复选框 = QCheckBox("使用虚拟环境")
        self.虚拟环境复选框.setChecked(当前设置.get("use_venv", True))
        虚拟环境布局.addWidget(self.虚拟环境复选框)

        虚拟环境说明 = QLabel(
            "默认勾选。勾选后，若项目目录下存在虚拟环境（.venv、venv、env），"
            "则优先使用虚拟环境中的 Python 解释器运行脚本；"
            "未检测到虚拟环境时回退到下方指定的解释器。"
        )
        虚拟环境说明.setWordWrap(True)
        虚拟环境说明.setStyleSheet("color: #666; font-size: 9pt;")
        虚拟环境布局.addWidget(虚拟环境说明)

        self.检测结果标签 = QLabel("")
        self.检测结果标签.setWordWrap(True)
        虚拟环境布局.addWidget(self.检测结果标签)

        主布局.addWidget(虚拟环境组)

        # ---------- 项目解释器（.pe 第 2 行，优先级最高） ----------
        项目解释器组 = QGroupBox("项目解释器")
        项目解释器布局 = QVBoxLayout(项目解释器组)
        self.项目解释器标签 = QLabel("")
        self.项目解释器标签.setWordWrap(True)
        项目解释器布局.addWidget(self.项目解释器标签)
        项目解释器说明 = QLabel(
            "在左侧文件树中右键 .exe 文件可“设置为项目解释器”，"
            "路径写入项目根目录 .pe 文件的第 2 行；"
            "为空或路径无效时，按下面的设置解析。"
        )
        项目解释器说明.setWordWrap(True)
        项目解释器说明.setStyleSheet("color: #666; font-size: 9pt;")
        项目解释器布局.addWidget(项目解释器说明)
        主布局.addWidget(项目解释器组)

        # ---------- 解释器路径 ----------
        分组框 = QGroupBox("Python 解释器路径")
        分组布局 = QVBoxLayout(分组框)

        说明标签 = QLabel("设置自定义 Python 解释器路径，留空则使用系统默认的 python 命令。\n"
                         "例如：C:\\Python312\\python.exe")
        说明标签.setWordWrap(True)
        说明标签.setStyleSheet("color: #666; font-size: 9pt;")
        分组布局.addWidget(说明标签)

        路径行 = QHBoxLayout()
        self.路径输入框 = QLineEdit()
        self.路径输入框.setPlaceholderText("留空使用默认 python")
        self.路径输入框.setText(当前设置.get("python_path", ""))
        self.路径输入框.setFont(self.路径输入框.font())  # 确保使用系统字体
        路径行.addWidget(self.路径输入框)

        浏览按钮 = QPushButton("浏览...")
        浏览按钮.clicked.connect(lambda: self._浏览文件())
        路径行.addWidget(浏览按钮)

        分组布局.addLayout(路径行)
        主布局.addWidget(分组框)

        主布局.addStretch()

        按钮布局 = QHBoxLayout()
        按钮布局.addStretch()

        保存按钮 = QPushButton("保存")
        保存按钮.clicked.connect(lambda: self._保存())
        取消按钮 = QPushButton("取消")
        取消按钮.clicked.connect(lambda: self.reject())

        按钮布局.addWidget(保存按钮)
        按钮布局.addWidget(取消按钮)
        主布局.addLayout(按钮布局)

        self._刷新检测结果()

    def _刷新检测结果(self):
        """展示当前项目目录下虚拟环境的检测结果"""
        虚拟环境 = 查找虚拟环境(self.项目目录)
        if 虚拟环境:
            颜色 = "#2E7D32"
            文本 = f"✔ 已检测到虚拟环境：{虚拟环境}\n{取解释器(虚拟环境)}"
        elif self.项目目录:
            颜色 = "#888"
            文本 = f"未在项目目录（{self.项目目录}）下检测到虚拟环境。"
        else:
            颜色 = "#888"
            文本 = "未打开项目，运行时将从脚本所在目录向上查找虚拟环境。"
        self.检测结果标签.setText(文本)
        self.检测结果标签.setStyleSheet(f"color: {颜色}; font-size: 9pt;")

        # 项目解释器（.pe 第 2 行）优先级最高，先展示它的状态
        原文, 绝对, 有效 = 项目解释器信息(self.项目目录)
        if 原文 and 有效:
            self.项目解释器标签.setText(f"✔ 当前项目解释器：{绝对}")
            self.项目解释器标签.setStyleSheet("color: #2E7D32; font-size: 9pt;")
        elif 原文:
            self.项目解释器标签.setText(
                f"⚠ 项目解释器路径无效：{原文}\n运行时将回退到下面的设置。"
            )
            self.项目解释器标签.setStyleSheet("color: #C62828; font-size: 9pt;")
        else:
            self.项目解释器标签.setText(
                "未设置项目解释器（.pe 第 2 行为空），按下面的设置解析。"
            )
            self.项目解释器标签.setStyleSheet("color: #888; font-size: 9pt;")

    def _浏览文件(self):
        路径, _ = QFileDialog.getOpenFileName(
            self, "选择 Python 解释器", "",
            "Python 解释器 (python.exe);;所有文件 (*)"
        )
        if 路径:
            self.路径输入框.setText(路径)

    def _保存(self):
        保存解释器设置({
            "python_path": self.路径输入框.text().strip(),
            "use_venv": self.虚拟环境复选框.isChecked(),
        })
        self.accept()
