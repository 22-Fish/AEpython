# 主界面.py - 界面（移除气泡，状态栏消息提示）
import os
from PyQt5.QtWidgets import (
    QMainWindow, QSplitter, QWidget,
    QVBoxLayout, QHBoxLayout, QAction, QLineEdit,
    QTreeView, QLabel, QFileSystemModel, QPushButton, QStatusBar,
    QPlainTextEdit
)
from PyQt5.QtGui import QFont, QKeySequence
from PyQt5.QtCore import Qt, QPoint, QDir
from GUI.代码编辑器 import 代码编辑器类


class 主界面类(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AEpy")
        self.resize(1200, 750)

        self.setStyleSheet("""
            QMenu::item { padding: 4px 20px 4px 10px; }
            QMenu::item:selected { background-color: #e0e0e0; }
            QMenu::indicator { width: 0px; }
        """)

        中心部件 = QWidget()
        self.setCentralWidget(中心部件)
        主布局 = QHBoxLayout(中心部件)
        主布局.setContentsMargins(0, 0, 0, 0)

        self.水平分割器 = QSplitter(Qt.Horizontal)
        主布局.addWidget(self.水平分割器)

        # ---------- 左侧文件树区域 ----------
        左侧容器 = QWidget()
        左侧布局 = QVBoxLayout(左侧容器)
        左侧布局.setContentsMargins(0, 0, 0, 0)
        左侧布局.setSpacing(0)

        self.文件夹标签 = QLabel("")
        self.文件夹标签.setFont(QFont("Microsoft YaHei", 10))
        self.文件夹标签.setStyleSheet("padding: 4px 6px; border-bottom: 1px solid #ccc;")
        左侧布局.addWidget(self.文件夹标签)

        self.文件树 = QTreeView()
        self.文件树.setHeaderHidden(True)
        self.文件树.setAnimated(True)
        self.文件模型 = QFileSystemModel()
        self.文件模型.setFilter(QDir.AllDirs | QDir.Files | QDir.NoDotAndDotDot)
        self.文件树.setModel(self.文件模型)
        self.文件树.setColumnHidden(1, True)
        self.文件树.setColumnHidden(2, True)
        self.文件树.setColumnHidden(3, True)
        self.文件树.setContextMenuPolicy(Qt.CustomContextMenu)
        左侧布局.addWidget(self.文件树)

        self.占位标签 = QLabel("未打开文件夹")
        self.占位标签.setFont(QFont("Microsoft YaHei", 10))
        self.占位标签.setAlignment(Qt.AlignCenter)
        self.占位标签.setStyleSheet("color: #888; background: transparent;")
        self.占位标签.setVisible(True)
        左侧布局.addWidget(self.占位标签)

        # 运行项目按钮
        self.运行项目按钮 = QPushButton("运行项目")
        self.运行项目按钮.setFont(QFont("Microsoft YaHei", 10))
        self.运行项目按钮.setStyleSheet("""
            QPushButton {
                color: white; background-color: #2c7a2c; border: none;
                padding: 8px 0px; border-radius: 4px;
            }
            QPushButton:hover { background-color: #349134; }
            QPushButton:disabled { background-color: #888; color: #ccc; }
        """)
        self.运行项目按钮.setEnabled(False)
        左侧布局.addWidget(self.运行项目按钮)

        self.水平分割器.addWidget(左侧容器)

        左侧容器.setVisible(True)
        self.文件树.setVisible(False)
        self.文件夹标签.setVisible(False)

        # ---------- 右侧编辑器与输出 ----------
        self.垂直分割器 = QSplitter(Qt.Vertical)
        self.水平分割器.addWidget(self.垂直分割器)

        self.文件名标签 = QLabel("未打开文件")
        self.文件名标签.setFont(QFont("Microsoft YaHei", 10))
        self.文件名标签.setStyleSheet("padding: 2px 8px; background: transparent; border: none;")
        self.文件名标签.setMaximumHeight(20)

        编辑器容器 = QWidget()
        编辑器布局 = QVBoxLayout(编辑器容器)
        编辑器布局.setContentsMargins(0, 0, 0, 0)
        编辑器布局.setSpacing(0)
        编辑器布局.addWidget(self.文件名标签)

        self.编辑器 = 代码编辑器类()
        self.编辑器.setReadOnly(True)
        编辑器布局.addWidget(self.编辑器)
        self.垂直分割器.addWidget(编辑器容器)

        输出容器 = QWidget()
        输出布局 = QVBoxLayout(输出容器)
        输出布局.setContentsMargins(0, 0, 0, 0)

        self.输出区 = QPlainTextEdit()
        self.输出区.setReadOnly(True)
        self.输出区.setFont(QFont("Microsoft YaHei", 11))
        # 输出框样式在 main.pyw 的 应用输出框样式() 中根据颜色配置动态设置
        输出布局.addWidget(self.输出区)

        参数布局 = QHBoxLayout()
        self.参数标签 = QLabel("运行参数:")
        self.参数标签.setFont(QFont("Microsoft YaHei", 9))
        self.参数输入框 = QLineEdit()
        self.参数输入框.setPlaceholderText("运行时参数（空格分隔）...")
        self.参数输入框.setFont(QFont("Microsoft YaHei", 10))
        参数布局.addWidget(self.参数标签)
        参数布局.addWidget(self.参数输入框)
        输出布局.addLayout(参数布局)

        self.垂直分割器.addWidget(输出容器)

        self.水平分割器.setSizes([160, 1040])
        self.垂直分割器.setSizes([580, 170])

        # 设置入口按钮（状态栏右侧）
        self.设置入口按钮 = QPushButton("⚙ 设置入口")
        self.设置入口按钮.setFont(QFont("Microsoft YaHei", 9))
        self.设置入口按钮.setFlat(True)
        self.设置入口按钮.setEnabled(False)
        self.设置入口按钮.setToolTip(
            "把当前打开的文件设为程序入口（也可在左侧文件树中右键文件设置）"
        )

        self.状态栏 = self.statusBar()
        self.状态栏.addPermanentWidget(self.设置入口按钮)

        self._创建菜单()

    def _创建菜单(self):
        菜单栏 = self.menuBar()

        文件菜单 = 菜单栏.addMenu("文件")
        self.新建动作 = QAction("新建源代码", self)
        self.新建动作.setShortcut(QKeySequence.New)
        文件菜单.addAction(self.新建动作)

        self.打开动作 = QAction("打开源代码", self)          # 修改处
        self.打开动作.setShortcut(QKeySequence.Open)
        文件菜单.addAction(self.打开动作)

        self.打开文件夹动作 = QAction("打开项目", self)      # 修改处
        self.打开文件夹动作.setShortcut(QKeySequence("Ctrl+Shift+O"))
        文件菜单.addAction(self.打开文件夹动作)

        文件菜单.addSeparator()
        self.保存动作 = QAction("保存", self)
        self.保存动作.setShortcut(QKeySequence.Save)
        文件菜单.addAction(self.保存动作)

        self.另存为动作 = QAction("另存为", self)
        self.另存为动作.setShortcut(QKeySequence("Ctrl+Shift+S"))
        文件菜单.addAction(self.另存为动作)

        文件菜单.addSeparator()
        self.关闭当前动作 = QAction("关闭当前文件或项目", self)
        self.关闭当前动作.setShortcut(QKeySequence("Ctrl+W"))
        文件菜单.addAction(self.关闭当前动作)

        self.退出动作 = QAction("退出", self)
        self.退出动作.setShortcut(QKeySequence("Ctrl+Q"))
        文件菜单.addAction(self.退出动作)

        # 运行文件
        self.运行文件动作 = QAction("运行文件", self)
        self.运行文件动作.setShortcut(QKeySequence("F5"))
        菜单栏.addAction(self.运行文件动作)

        # 停止程序
        self.停止程序动作 = QAction("停止程序", self)
        菜单栏.addAction(self.停止程序动作)

        # 样式
        self.样式动作 = QAction("样式", self)
        菜单栏.addAction(self.样式动作)

        # 设置菜单
        self.设置菜单 = 菜单栏.addMenu("设置")
        self.解释器设置动作 = QAction("解释器设置", self)
        self.设置菜单.addAction(self.解释器设置动作)

    def 更新文件名标签(self, 文本):
        self.文件名标签.setText(文本)

    def 更新文件夹标签(self, 文本):
        self.文件夹标签.setText(文本)

    def 显示文件树(self, 显示):
        self.文件树.setVisible(显示)
        self.文件夹标签.setVisible(显示)
        self.占位标签.setVisible(not 显示)
