# 左侧文件树.py - 文件树右键菜单（重命名修复，消除菜单左侧竖线）
import os
from PyQt5.QtWidgets import (
    QMenu, QInputDialog, QMessageBox, QDialog, QVBoxLayout,
    QLineEdit, QPushButton, QLabel, QHBoxLayout
)
from PyQt5.QtGui import QFont
from PyQt5.QtCore import QDir, Qt

from config.路径 import 读取JSON, 保存JSON
from config.项目入口 import 项目解释器信息


class 重命名对话框(QDialog):
    def __init__(self, 旧路径, 父=None):
        super().__init__(父)
        self.旧路径 = 旧路径
        self.新名称 = ""
        self.设置界面()

    def 设置界面(self):
        self.setWindowTitle("重命名文件")
        self.setFixedSize(400, 120)
        布局 = QVBoxLayout(self)
        提示 = QLabel("请输入新名称：")
        提示.setFont(QFont("Microsoft YaHei", 10))
        布局.addWidget(提示)

        self.输入框 = QLineEdit()
        self.输入框.setFont(QFont("Microsoft YaHei", 10))
        原名 = os.path.basename(self.旧路径)
        self.输入框.setText(原名)
        基础, 扩展 = os.path.splitext(原名)
        if 基础:
            self.输入框.setSelection(0, len(基础))
        布局.addWidget(self.输入框)

        按钮布局 = QHBoxLayout()
        确定 = QPushButton("确定")
        确定.clicked.connect(lambda: self.确定())
        取消 = QPushButton("取消")
        取消.clicked.connect(self.reject)
        按钮布局.addStretch()
        按钮布局.addWidget(确定)
        按钮布局.addWidget(取消)
        布局.addLayout(按钮布局)

    def 确定(self):
        self.新名称 = self.输入框.text().strip()
        if not self.新名称:
            QMessageBox.warning(self, "错误", "文件名不能为空")
            return
        if self.新名称 == os.path.basename(self.旧路径):
            QMessageBox.warning(self, "错误", "请输入不同的文件名")
            return
        if any(c in self.新名称 for c in r'\/:*?"<>|'):
            QMessageBox.warning(self, "错误", "文件名包含非法字符")
            return
        self.accept()

    def 获取新名称(self):
        return self.新名称


class 文件树管理器:
    def __init__(self, 主窗口, 文件树, 文件模型, 文件夹标签, 状态栏):
        self.主窗口 = 主窗口
        self.文件树 = 文件树
        self.文件模型 = 文件模型
        self.文件夹标签 = 文件夹标签
        self.状态栏 = 状态栏
        self.当前文件夹 = None
        self.当前文件 = None

        self.文件树.doubleClicked.connect(lambda 索引: self.文件树打开(索引))
        self.文件树.customContextMenuRequested.connect(lambda pos: self.显示右键菜单(pos))

    def 打开文件夹(self, 文件夹路径):
        self.当前文件夹 = 文件夹路径
        self.文件模型.setRootPath(文件夹路径)
        根索引 = self.文件模型.index(文件夹路径)
        self.文件树.setRootIndex(根索引)
        self.文件树.setExpanded(根索引, True)
        self.文件夹标签.setText(f"📁 {os.path.basename(文件夹路径)} ▾")
        self.状态栏.showMessage(f"项目文件夹: {文件夹路径}")
        self.保存最近文件夹(文件夹路径)

    def 关闭文件夹(self):
        self.当前文件夹 = None
        self.文件夹标签.setText("")
        self.文件树.setRootIndex(self.文件模型.index(QDir.rootPath()).parent())

    def 保存最近文件夹(self, 文件夹):
        保存JSON("last_folder.json", {"last_folder": 文件夹})

    def 恢复最近文件夹(self):
        数据 = 读取JSON("last_folder.json", {}) or {}
        文件夹 = 数据.get("last_folder") if isinstance(数据, dict) else None
        if 文件夹 and os.path.isdir(文件夹):
            return 文件夹
        return None

    def 文件树打开(self, 索引):
        路径 = self.文件模型.filePath(索引)
        if os.path.isfile(路径):
            self.主窗口.请求打开文件(路径)

    # ---------- 程序入口 / 项目解释器 ----------
    def _当前项目解释器(self):
        """当前项目已配置的项目解释器原文（未配置返回 ""）"""
        if not self.当前文件夹:
            return ""
        原文, _, _ = 项目解释器信息(self.当前文件夹)
        return 原文

    def _设置程序入口(self, 路径):
        self.主窗口.设置入口为(路径)

    def _设置项目解释器(self, 路径):
        self.主窗口.设置项目解释器为(路径)

    def _清除项目解释器(self):
        self.主窗口.清除项目解释器()

    def 显示右键菜单(self, pos):
        索引 = self.文件树.indexAt(pos)
        菜单 = QMenu()
        # 消除左侧竖线（去除指示器、图标占位、左内边距）
        菜单.setStyleSheet("""
            QMenu::indicator { width: 0px; }
            QMenu::item { padding: 4px 20px 4px 10px; border: none; }
            QMenu { border: 1px solid #ccc; }
        """)
        if 索引.isValid():
            路径 = self.文件模型.filePath(索引)
            if os.path.isdir(路径):
                菜单.addAction("新建文件", lambda: self.在文件夹新建文件(路径))
                菜单.addSeparator()
                菜单.addAction("新建文件夹", lambda: self.新建文件夹(路径))
                菜单.addSeparator()
                菜单.addAction("删除文件夹", lambda: self.删除文件夹(路径))
            else:
                菜单.addAction("重命名", lambda: self.重命名文件(路径))
                菜单.addAction("删除", lambda: self.删除文件(路径))
                菜单.addSeparator()
                # 程序入口 / 项目解释器（.pe 第 1 行、第 2 行）
                菜单.addAction("设置为程序入口", lambda: self._设置程序入口(路径))
                if os.path.splitext(路径)[1].lower() == ".exe":
                    菜单.addAction("设置为项目解释器", lambda: self._设置项目解释器(路径))
                if self._当前项目解释器():
                    菜单.addAction("清除项目解释器", lambda: self._清除项目解释器())
        else:
            if self.当前文件夹:
                菜单.addAction("新建文件", lambda: self.在文件夹新建文件(self.当前文件夹))
                菜单.addSeparator()
                菜单.addAction("新建文件夹", lambda: self.新建文件夹(self.当前文件夹))
            else:
                return
        菜单.exec_(self.文件树.viewport().mapToGlobal(pos))

    def 新建文件夹(self, 文件夹路径):
        名称, ok = QInputDialog.getText(None, "新建文件夹", "请输入文件夹名称:")
        if ok and 名称.strip():
            新目录 = os.path.join(文件夹路径, 名称.strip())
            if os.path.exists(新目录):
                QMessageBox.warning(None, "错误", "名称已存在")
                return
            if any(c in 名称.strip() for c in r'\\/:*?"<>|'):
                QMessageBox.warning(None, "错误", "名称包含非法字符")
                return
            try:
                os.makedirs(新目录)
                self.状态栏.showMessage(f"已创建文件夹: {新目录}")
            except Exception as e:
                QMessageBox.critical(None, "创建失败", str(e))

    def 删除文件夹(self, 路径):
        文件夹名 = os.path.basename(路径)
        确认 = QMessageBox.question(None, "删除文件夹",
                                    f"确定永久删除文件夹 {文件夹名} 及其所有内容吗？",
                                    QMessageBox.Yes | QMessageBox.No)
        if 确认 == QMessageBox.Yes:
            try:
                import shutil
                shutil.rmtree(路径)
                self.状态栏.showMessage(f"已删除文件夹: {路径}")
            except Exception as e:
                QMessageBox.critical(None, "删除失败", str(e))

    def 在文件夹新建文件(self, 文件夹):
        名称, ok = QInputDialog.getText(None, "新建文件", "请输入新名称（含扩展名）:")
        if ok and 名称.strip():
            新路径 = os.path.join(文件夹, 名称.strip())
            if os.path.exists(新路径):
                QMessageBox.warning(None, "错误", "文件已存在")
                return
            try:
                open(新路径, "w").close()
                self.状态栏.showMessage(f"已创建: {新路径}")
                新索引 = self.文件模型.index(新路径)
                if 新索引.isValid():
                    self.文件树.setCurrentIndex(新索引)
            except Exception as e:
                QMessageBox.critical(None, "创建失败", str(e))

    def 重命名文件(self, 原路径):
        对话框 = 重命名对话框(原路径)
        if 对话框.exec_() != QDialog.Accepted:
            return
        新名称 = 对话框.获取新名称()
        新路径 = os.path.join(os.path.dirname(原路径), 新名称)
        if os.path.exists(新路径):
            回复 = QMessageBox.question(None, "文件已存在", f"{新名称} 已存在，是否覆盖？",
                                        QMessageBox.Yes | QMessageBox.No)
            if 回复 != QMessageBox.Yes:
                return

        if self.当前文件 == 原路径:
            try:
                内容 = self.主窗口.获取编辑器内容()
                with open(新路径, "w", encoding="utf-8") as f:
                    f.write(内容)
                os.remove(原路径)
                self.主窗口.设置当前文件(新路径)
                self.状态栏.showMessage(f"已重命名为: {新路径}")
                索引 = self.文件模型.index(新路径)
                if 索引.isValid():
                    self.文件树.setCurrentIndex(索引)
            except Exception as e:
                QMessageBox.critical(None, "重命名失败", str(e))
        else:
            try:
                os.replace(原路径, 新路径)
                self.状态栏.showMessage(f"已重命名为: {新路径}")
                索引 = self.文件模型.index(新路径)
                if 索引.isValid():
                    self.文件树.setCurrentIndex(索引)
            except Exception as e:
                QMessageBox.critical(None, "重命名失败", str(e))

    def 删除文件(self, 路径):
        确认 = QMessageBox.question(None, "删除文件",
                                    f"确定永久删除 {os.path.basename(路径)} 吗？",
                                    QMessageBox.Yes | QMessageBox.No)
        if 确认 == QMessageBox.Yes:
            try:
                if self.当前文件 == 路径:
                    self.主窗口.清除当前文件()
                    self.当前文件 = None
                os.remove(路径)
                self.状态栏.showMessage(f"已删除: {路径}")
            except Exception as e:
                QMessageBox.critical(None, "删除失败", str(e))
