# 高亮设置.py - 语法高亮设置弹窗
import logging
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QCheckBox, QPushButton, QLabel, QGroupBox
)
from PyQt5.QtCore import Qt

from config.路径 import 读取JSON, 保存JSON

logger = logging.getLogger('设置')
logger.setLevel(logging.DEBUG)

配置文件 = "highlight_settings.json"


def 读取设置():
    数据 = 读取JSON(配置文件, {"utf16_对齐修复": False})
    return 数据 if isinstance(数据, dict) else {"utf16_对齐修复": False}


def 保存设置(设置):
    保存JSON(配置文件, 设置)
    logger.info(f"设置已保存: {设置}")


class 设置对话框(QDialog):
    def __init__(self, 父窗口=None):
        super().__init__(父窗口)
        self.setWindowTitle("设置")
        self.resize(420, 200)

        self.设置 = 读取设置()
        self.结果 = self.设置.copy()

        主布局 = QVBoxLayout(self)
        主布局.setSpacing(12)

        分组框 = QGroupBox("字符编码兼容")
        utf16布局 = QVBoxLayout(分组框)

        self.utf16复选框 = QCheckBox("UTF-16代码对齐修复")
        self.utf16复选框.setChecked(self.设置.get("utf16_对齐修复", False))
        说明utf16 = QLabel(
            "修复包含 emoji（📄📁🐛 等）的文件中语法高亮向左偏移一格的问题。\n"
            "启用后性能可能略有下降。"
        )
        说明utf16.setWordWrap(True)
        说明utf16.setStyleSheet("color: #666; font-size: 9pt;")
        utf16布局.addWidget(self.utf16复选框)
        utf16布局.addWidget(说明utf16)
        主布局.addWidget(分组框)

        主布局.addStretch()
        按钮布局 = QHBoxLayout()
        按钮布局.addStretch()

        保存按钮 = QPushButton("保存")
        保存按钮.clicked.connect(self._on_save)
        取消按钮 = QPushButton("取消")
        取消按钮.clicked.connect(self.reject)

        按钮布局.addWidget(保存按钮)
        按钮布局.addWidget(取消按钮)
        主布局.addLayout(按钮布局)

    def _on_save(self):
        self.结果["utf16_对齐修复"] = self.utf16复选框.isChecked()
        保存设置(self.结果)
        self.accept()

    def 获取结果(self):
        return self.结果
