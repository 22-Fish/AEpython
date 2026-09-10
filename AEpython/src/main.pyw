# main.pyw - 主逻辑（增加任务栏独立图标）
import os
import sys
import ctypes

# 路径引导：把 src 目录加入 sys.path，保证以任意方式启动都能导入下面的包
_本目录 = os.path.dirname(os.path.abspath(__file__))
if _本目录 not in sys.path:
    sys.path.insert(0, _本目录)


def _重定向标准流():
    """GUI 模式下可能没有控制台，把标准流指向 NUL，避免写无效句柄报错或阻塞"""
    try:
        空设备 = open(os.devnull, "w", encoding="utf-8")
    except OSError:
        return
    for 名称 in ("stdout", "stderr"):
        流 = getattr(sys, 名称, None)
        需要替换 = 流 is None
        if not 需要替换:
            try:
                需要替换 = not 流.isatty()
            except Exception:
                需要替换 = True
        if 需要替换:
            setattr(sys, 名称, 空设备)


_重定向标准流()

# 设置独立应用模式
myappid = 'aepy.python.editor.1.0'
ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)

from PyQt5.QtWidgets import (
    QApplication, QFileDialog, QMessageBox, QShortcut, QDialog
)
from PyQt5.QtGui import QFont, QKeySequence, QIcon
from PyQt5.QtCore import QTimer, QProcess

from config.路径 import 配置文件路径, 获取资源目录, 读取JSON, 保存JSON
from config.解释器配置 import 读取解释器设置
from config.项目入口 import (
    读取入口信息, 写入入口信息, 解析入口路径, 项目解释器信息, 转存储路径
)
from GUI.主界面 import 主界面类
from GUI.语法高亮器 import Python语法高亮器
from GUI.解释器设置 import 解释器设置对话框
from GUI.左侧文件树 import 文件树管理器
from GUI.自定义样式 import 样式设置对话框
from GUI.高亮设置 import 设置对话框, 读取设置
from 解释器.调用解释器 import 脚本运行器


class 主窗口(主界面类):
    def __init__(self):
        super().__init__()
        self.当前文件 = None
        self.颜色配置 = self._加载颜色配置()
        self.运行器 = 脚本运行器(self.输出区, self.statusBar(), self.颜色配置,
                                  解释器设置=读取解释器设置())
        self.文件树管理 = 文件树管理器(
            self, self.文件树, self.文件模型, self.文件夹标签, self.statusBar()
        )
        self.高亮器 = Python语法高亮器(self.编辑器.document(), self.颜色配置,
                                        utf16对齐修复=读取设置().get("utf16_对齐修复", False))
        self.加载字体配置()
        self.编辑器.更新颜色配置(self.颜色配置)
        self.应用输出框样式()
        self._连接信号()
        self._创建快捷键()
        self.运行器.进程.finished.connect(lambda: self.恢复参数输入模式())
        QTimer.singleShot(0, lambda: self.恢复上次状态())

    def _加载颜色配置(self):
        配置 = 读取JSON("colors.json", None)
        if not isinstance(配置, dict):
            配置 = Python语法高亮器.默认颜色配置()
        # 旧键名兼容：程序输出 → 解释器输出（只作用于 stderr）；系统消息 → 系统输出消息
        if "解释器输出" not in 配置 and "程序输出" in 配置:
            配置["解释器输出"] = 配置["程序输出"]
        配置.pop("程序输出", None)
        if "系统输出消息" not in 配置 and "系统消息" in 配置:
            配置["系统输出消息"] = 配置["系统消息"]
        配置.pop("系统消息", None)
        # 补充配置文件可能缺失的默认字段
        默认 = Python语法高亮器.默认颜色配置()
        for key in ["默认背景色", "当前行背景色", "括号匹配", "行号", "光标",
                    "解释器输出", "系统输出消息"]:
            if key not in 配置:
                配置[key] = 默认[key]
        return 配置

    def 加载字体配置(self):
        # 读取 font.json 中的 font/font_size 并应用到编辑器
        # 注意：编辑器自身的 _加载字体配置() 中已有完整逻辑，
        # 此方法仅用于主窗口初始化时读取配置，
        # 改为直接委托编辑器自己的重新加载方法，避免双重初始化覆盖
        if hasattr(self, '编辑器') and hasattr(self.编辑器, '重新加载字体配置'):
            self.编辑器.重新加载字体配置()

    # ---------- 状态记忆（文件与文件夹互斥） ----------
    def _清除文件记忆(self):
        路径 = 配置文件路径("last_file.json")
        if os.path.exists(路径):
            os.remove(路径)

    def _清除文件夹记忆(self):
        路径 = 配置文件路径("last_folder.json")
        if os.path.exists(路径):
            os.remove(路径)

    def 保存最近文件(self, 文件路径):
        保存JSON("last_file.json", {"last_file": 文件路径})

    def 恢复最近文件(self):
        数据 = 读取JSON("last_file.json", {}) or {}
        文件 = 数据.get("last_file") if isinstance(数据, dict) else None
        if 文件 and os.path.isfile(文件):
            return 文件
        return None

    def 恢复上次状态(self):
        # 恢复分割器位置（文件树打开前后宽度一致）
        self._恢复分割器状态()
        # 优先恢复上次打开的单个文件
        文件 = self.恢复最近文件()
        if 文件:
            try:
                self._加载文件内容(文件)
                self.文件树管理.当前文件 = self.当前文件
                self.显示文件树(False)
                self.刷新入口按钮状态()
                return
            except:
                pass
        # 否则恢复文件夹
        文件夹 = self.文件树管理.恢复最近文件夹()
        if 文件夹:
            self.打开文件夹(文件夹, 询问保存=False)

    def _连接信号(self):
        self.新建动作.triggered.connect(lambda: self.新建文件())
        self.打开动作.triggered.connect(lambda: self.打开文件())
        self.打开文件夹动作.triggered.connect(lambda: self.选择文件夹())
        self.保存动作.triggered.connect(lambda: self.保存文件())
        self.另存为动作.triggered.connect(lambda: self.文件另存为())
        self.关闭当前动作.triggered.connect(lambda: self.关闭当前())
        self.退出动作.triggered.connect(lambda: self.close())
        self.运行文件动作.triggered.connect(lambda: self.运行脚本())
        self.停止程序动作.triggered.connect(lambda: self.停止程序())
        self.样式动作.triggered.connect(lambda: self.打开样式())
        self.解释器设置动作.triggered.connect(lambda: self.打开解释器设置())
        self.设置入口按钮.clicked.connect(lambda: self.设置入口())
        self.运行项目按钮.clicked.connect(lambda: self.运行入口脚本())

    def _创建快捷键(self):
        self.快捷键_新建 = QShortcut(QKeySequence("Ctrl+N"), self, lambda: self.新建文件())
        self.快捷键_打开 = QShortcut(QKeySequence("Ctrl+O"), self, lambda: self.打开文件())
        self.快捷键_保存 = QShortcut(QKeySequence("Ctrl+S"), self, lambda: self.保存文件())
        self.快捷键_另存为 = QShortcut(QKeySequence("Ctrl+Shift+S"), self, lambda: self.文件另存为())
        self.快捷键_运行 = QShortcut(QKeySequence("F5"), self, lambda: self.运行脚本())

    def closeEvent(self, event):
        if not self.maybe_save():
            event.ignore()
            return
        # 保存左右分割器宽度
        self._保存分割器状态()
        # 根据当前模式保存状态（互斥）
        if self.文件树.isVisible() and self.文件树管理.当前文件夹:
            # 文件夹模式：只保存文件夹，清除文件记忆
            self.文件树管理.保存最近文件夹(self.文件树管理.当前文件夹)
            self._清除文件记忆()
        elif self.当前文件:
            # 单文件模式：只保存文件，清除文件夹记忆
            self.保存最近文件(self.当前文件)
            self._清除文件夹记忆()
        else:
            # 无文件无文件夹，清除所有记忆
            self._清除文件记忆()
            self._清除文件夹记忆()
        event.accept()

    def _保存分割器状态(self):
        """保存水平分割器和垂直分割器的大小到配置文件"""
        保存JSON("splitter.json", {
            "水平分割器": self.水平分割器.sizes(),
            "垂直分割器": self.垂直分割器.sizes()
        })

    def _恢复分割器状态(self):
        """从配置文件恢复分割器大小"""
        配置 = 读取JSON("splitter.json", None)
        if not isinstance(配置, dict):
            return
        try:
            水平 = 配置.get("水平分割器")
            if 水平 and len(水平) == 2:
                self.水平分割器.setSizes(水平)
            垂直 = 配置.get("垂直分割器")
            if 垂直 and len(垂直) == 2:
                self.垂直分割器.setSizes(垂直)
        except (TypeError, ValueError):
            pass

    def maybe_save(self):
        微软雅黑字体 = QFont("Microsoft YaHei", 10)
        if not self.当前文件:
            if self.编辑器.toPlainText().strip():
                消息盒 = QMessageBox(self)
                消息盒.setWindowTitle("未保存的修改")
                消息盒.setText("当前文档未保存，是否保存？")
                消息盒.setStandardButtons(QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel)
                消息盒.button(QMessageBox.Save).setText("保存")
                消息盒.button(QMessageBox.Discard).setText("丢弃")
                消息盒.button(QMessageBox.Cancel).setText("取消")
                消息盒.setFont(微软雅黑字体)
                回复 = 消息盒.exec_()
                if 回复 == QMessageBox.Save:
                    self.文件另存为()
                    return self.当前文件 is not None
                elif 回复 == QMessageBox.Cancel:
                    return False
            return True

        ext = os.path.splitext(self.当前文件)[1].lower()
        if ext in ('.py', '.pyw'):
            self.保存文件()
            return True

        try:
            with open(self.当前文件, "r", encoding="utf-8") as f:
                磁盘 = f.read()
        except:
            磁盘 = None
        if self.编辑器.toPlainText() != 磁盘:
            消息盒 = QMessageBox(self)
            消息盒.setWindowTitle("未保存的修改")
            消息盒.setText(f"文件 {os.path.basename(self.当前文件)} 已被修改，是否保存？")
            消息盒.setStandardButtons(QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel)
            消息盒.button(QMessageBox.Save).setText("保存")
            消息盒.button(QMessageBox.Discard).setText("丢弃")
            消息盒.button(QMessageBox.Cancel).setText("取消")
            消息盒.setFont(微软雅黑字体)
            回复 = 消息盒.exec_()
            if 回复 == QMessageBox.Save:
                self.保存文件()
                return True
            elif 回复 == QMessageBox.Cancel:
                return False
        return True

    # ---------- 文件操作 ----------
    def 新建文件(self):
        if not self.maybe_save():
            return
        self.编辑器.clear()
        self.当前文件 = None
        self.文件树管理.当前文件 = None
        self.编辑器.setReadOnly(True)
        self.更新文件名标签("未命名")
        self.setWindowTitle("AEpy")
        self.显示文件树(False)
        self.刷新入口按钮状态()

    def 打开文件(self, 文件路径=None):
        if not self.maybe_save():
            return
        if not 文件路径:
            文件路径, _ = QFileDialog.getOpenFileName(self, "打开 Python 文件", "",
                                                      "Python 文件 (*.py);;所有文件 (*)")
        if 文件路径:
            self._加载文件内容(文件路径)
            self.显示文件树(False)
            self.文件树管理.当前文件 = self.当前文件
            self.刷新入口按钮状态()

    def 从文件树打开文件(self, 文件路径):
        if not self.maybe_save():
            return
        self._加载文件内容(文件路径)
        self.文件树管理.当前文件 = self.当前文件
        self.刷新入口按钮状态()

    def _加载文件内容(self, 文件路径):
        try:
            with open(文件路径, "r", encoding="utf-8") as f:
                内容 = f.read()
            self.编辑器.setPlainText(内容)
            self.当前文件 = 文件路径
            self.编辑器.setReadOnly(False)
            self.更新文件名标签(os.path.basename(文件路径))
            self.setWindowTitle(f"AEpy - {文件路径}")
        except Exception as e:
            QMessageBox.critical(self, "打开错误", str(e))

    def 选择文件夹(self):
        if not self.maybe_save():
            return
        文件夹 = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if 文件夹:
            self.打开文件夹(文件夹)

    def 打开文件夹(self, 文件夹路径, 询问保存=True):
        if 询问保存 and not self.maybe_save():
            return
        self.文件树管理.打开文件夹(文件夹路径)
        self.运行器.设置项目目录(文件夹路径)
        self.显示文件树(True)
        self.setWindowTitle(f"AEpy - {文件夹路径}")
        self.加载入口脚本()
        self.刷新入口按钮状态()

    def 保存文件(self):
        if self.当前文件:
            try:
                with open(self.当前文件, "w", encoding="utf-8") as f:
                    f.write(self.编辑器.toPlainText())
                self.statusBar().showMessage(f"已保存: {self.当前文件}")
            except Exception as e:
                QMessageBox.critical(self, "保存错误", str(e))
        else:
            self.文件另存为()

    def 文件另存为(self):
        路径, _ = QFileDialog.getSaveFileName(self, "保存 Python 文件", "",
                                               "Python 文件 (*.py);;所有文件 (*)")
        if 路径:
            self.当前文件 = 路径
            self.保存文件()
            self.更新文件名标签(os.path.basename(路径))
            self.编辑器.setReadOnly(False)
            self.setWindowTitle(f"AEpy - {路径}")

    def 关闭当前(self):
        if not self.maybe_save():
            return
        self.编辑器.clear()
        之前是文件夹模式 = self.文件树.isVisible()
        self.当前文件 = None
        self.文件树管理.当前文件 = None
        self.编辑器.setReadOnly(True)
        self.更新文件名标签("未打开文件")
        self.setWindowTitle("AEpy")
        self.显示文件树(False)
        if 之前是文件夹模式:
            self.文件树管理.关闭文件夹()
        self.运行器.设置项目目录(None)
        self.刷新入口按钮状态()

    def 请求打开文件(self, 路径):
        self.从文件树打开文件(路径)

    def 获取编辑器内容(self):
        return self.编辑器.toPlainText()

    def 设置当前文件(self, 路径):
        self.当前文件 = 路径
        self.文件树管理.当前文件 = 路径
        self.更新文件名标签(os.path.basename(路径))
        self.setWindowTitle(f"AEpy - {路径}")
        self.编辑器.setReadOnly(False)

    def 清除当前文件(self):
        self.编辑器.clear()
        self.当前文件 = None
        self.文件树管理.当前文件 = None
        self.编辑器.setReadOnly(True)
        self.更新文件名标签("未打开文件")
        self.setWindowTitle("AEpy")

    # ---------- 入口脚本 ----------
    def 加载入口脚本(self):
        self.刷新入口按钮状态()

    def _项目内相对路径(self, 文件路径):
        """把文件路径换算成项目内的相对路径；不在项目内（或参数为空）返回 None"""
        if not self.文件树管理.当前文件夹 or not 文件路径:
            return None
        try:
            项目绝对 = os.path.abspath(self.文件树管理.当前文件夹)
            文件绝对 = os.path.abspath(文件路径)
            if os.path.commonpath([文件绝对, 项目绝对]) != 项目绝对:
                return None
        except ValueError:
            return None
        return os.path.relpath(文件绝对, 项目绝对)

    def _确认对话框(self, 文本):
        """统一的“是/否”确认框"""
        消息盒 = QMessageBox(self)
        消息盒.setWindowTitle("确认")
        消息盒.setText(文本)
        消息盒.setStandardButtons(QMessageBox.Yes | QMessageBox.No)
        消息盒.button(QMessageBox.Yes).setText("是")
        消息盒.button(QMessageBox.No).setText("否")
        消息盒.setFont(QFont("Microsoft YaHei", 10))
        return 消息盒.exec_() == QMessageBox.Yes

    def 设置入口(self):
        """状态栏“设置入口”按钮：把当前打开的文件设为程序入口"""
        self.设置入口为(self.当前文件)

    def 设置入口为(self, 文件路径):
        """把指定文件设为项目程序入口（写 .pe 第 1 行，保留第 2 行的项目解释器）

        .py/.pyw 由解释器运行；.exe/.bat/.cmd 作为可执行程序直接运行。
        """
        if not self.文件树管理.当前文件夹:
            QMessageBox.warning(self, "提示", "请先打开一个文件夹")
            return
        if not 文件路径:
            QMessageBox.warning(self, "提示", "请先打开一个文件")
            return
        相对路径 = self._项目内相对路径(文件路径)
        if 相对路径 is None:
            QMessageBox.warning(self, "错误", "该文件不在项目文件夹内")
            return
        if not self._确认对话框(f"是否将程序入口改为 {相对路径} ？"):
            return

        项目绝对 = os.path.abspath(self.文件树管理.当前文件夹)
        _, 项目解释器 = 读取入口信息(项目绝对)
        try:
            写入入口信息(项目绝对, 相对路径, 项目解释器)
            self.刷新入口按钮状态()
            self.状态栏.showMessage(f"入口已设置为: {相对路径}")
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    def 设置项目解释器为(self, 文件路径):
        """把某个 .exe 设为项目解释器（写入 .pe 第 2 行）

        项目解释器为空或路径无效时，运行时按“解释器设置”的原逻辑解析。
        """
        if not self.文件树管理.当前文件夹:
            QMessageBox.warning(self, "提示", "请先打开一个文件夹")
            return
        if not 文件路径 or os.path.splitext(文件路径)[1].lower() != ".exe":
            QMessageBox.warning(self, "提示", "请选择 .exe 文件作为项目解释器")
            return
        项目绝对 = os.path.abspath(self.文件树管理.当前文件夹)
        存储路径 = 转存储路径(项目绝对, 文件路径)
        if not self._确认对话框(
            f"是否将项目解释器设置为 {存储路径} ？\n"
            f"项目入口程序会用该解释器运行（留空则按“解释器设置”解析）。"
        ):
            return
        入口, _ = 读取入口信息(项目绝对)
        try:
            写入入口信息(项目绝对, 入口, 存储路径)
            self.刷新入口按钮状态()
            self.状态栏.showMessage(f"项目解释器已设置为: {存储路径}")
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    def 清除项目解释器(self):
        """清空 .pe 第 2 行，恢复按“解释器设置”解析解释器"""
        if not self.文件树管理.当前文件夹:
            return
        项目绝对 = os.path.abspath(self.文件树管理.当前文件夹)
        入口, 项目解释器 = 读取入口信息(项目绝对)
        if not 项目解释器:
            QMessageBox.information(self, "提示", "当前项目没有设置项目解释器。")
            return
        if not self._确认对话框(
            f"是否清除项目解释器（{项目解释器}）？\n清除后将按“解释器设置”解析解释器。"
        ):
            return
        try:
            写入入口信息(项目绝对, 入口, "")
            self.刷新入口按钮状态()
            self.状态栏.showMessage("项目解释器已清除")
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    def 运行入口脚本(self):
        if not self.文件树管理.当前文件夹:
            QMessageBox.warning(self, "提示", "请先打开一个文件夹")
            return
        项目绝对 = os.path.abspath(self.文件树管理.当前文件夹)
        入口文件路径 = os.path.join(项目绝对, ".pe")
        if not os.path.isfile(入口文件路径):
            QMessageBox.warning(self, "提示", "未找到 .pe 文件，请先设置入口。")
            return
        绝对路径 = 解析入口路径(项目绝对)
        if not 绝对路径:
            QMessageBox.warning(self, "提示", ".pe 文件为空。")
            return
        if not os.path.isfile(绝对路径):
            QMessageBox.warning(self, "提示", f"入口文件不存在：{绝对路径}")
            return
        # 项目解释器无效时给出提示（运行时会自动回退到解释器设置的原逻辑）
        原文, _, 有效 = 项目解释器信息(项目绝对)
        if 原文 and not 有效:
            self.状态栏.showMessage(f"项目解释器无效（{原文}），已回退到解释器设置")

        if self.运行器.进程.state() != QProcess.NotRunning:
            确认 = QMessageBox.question(self, "程序正在运行",
                                        "当前有程序正在运行，是否停止并运行新程序？",
                                        QMessageBox.Yes | QMessageBox.No)
            if 确认 != QMessageBox.Yes:
                return
            self.运行器.停止()

        if self.当前文件:
            ext = os.path.splitext(self.当前文件)[1].lower()
            if ext in ('.py', '.pyw'):
                if not self.maybe_save():
                    return

        if self.运行器.运行(绝对路径, self.参数输入框.text()):
            self.启动运行时输入模式()
            self.状态栏.showMessage("项目运行已启动")

    def 刷新入口按钮状态(self):
        文件夹模式 = self.文件树.isVisible()
        self.设置入口按钮.setEnabled(文件夹模式 and self.当前文件 is not None)
        运行项目可用 = False
        if 文件夹模式 and self.文件树管理.当前文件夹:
            入口 = 解析入口路径(os.path.abspath(self.文件树管理.当前文件夹))
            if 入口 and os.path.isfile(入口):
                运行项目可用 = True
        self.运行项目按钮.setEnabled(运行项目可用)

    # ---------- 运行/停止/input ----------
    def 运行脚本(self):
        if not self.当前文件:
            return
        if self.运行器.进程.state() != QProcess.NotRunning:
            确认 = QMessageBox.question(self, "程序正在运行",
                                        "当前有程序正在运行，是否停止并运行新程序？",
                                        QMessageBox.Yes | QMessageBox.No)
            if 确认 != QMessageBox.Yes:
                return
            self.运行器.停止()
        if not self.maybe_save():
            return
        if self.运行器.运行(self.当前文件, self.参数输入框.text()):
            self.启动运行时输入模式()
            self.状态栏.showMessage("文件运行已启动")

    def 停止程序(self):
        确认 = QMessageBox.question(self, "确认", "确定要停止当前程序吗？",
                                    QMessageBox.Yes | QMessageBox.No)
        if 确认 == QMessageBox.Yes:
            self.运行器.停止()
            self.恢复参数输入模式()

    def 启动运行时输入模式(self):
        self.参数标签.setVisible(False)
        self.参数输入框.setStyleSheet(
            "background-color: #2b2b2b; color: white; border: none; padding: 4px 8px; font-size: 11pt;"
        )
        self.参数输入框.setPlaceholderText("在此输入，回车发送...")
        self.参数输入框.clear()
        try:
            self.参数输入框.returnPressed.disconnect()
        except TypeError:
            pass
        self.参数输入框.returnPressed.connect(lambda: self.发送输入())
        self.参数输入框.setFocus()

    def 恢复参数输入模式(self):
        self.参数标签.setVisible(True)
        self.参数输入框.setStyleSheet("")
        self.参数输入框.setPlaceholderText("运行时参数（空格分隔）...")
        self.参数输入框.clear()
        try:
            self.参数输入框.returnPressed.disconnect()
        except TypeError:
            pass

    def 发送输入(self):
        文本 = self.参数输入框.text()
        if 文本.strip():
            self.运行器.发送输入(文本)
            self.参数输入框.clear()

    def 打开样式(self):
        当前字体名 = self.编辑器.font().families()[0]
        对话框 = 样式设置对话框(当前字体名, self.颜色配置, self)
        if 对话框.exec_() == QDialog.Accepted:
            self.加载字体配置()
            self.颜色配置 = 对话框.颜色配置
            self.高亮器.重新应用配置(self.颜色配置)
            self.编辑器.更新颜色配置(self.颜色配置)
            self.运行器.更新颜色配置(self.颜色配置)
            self.应用输出框样式()

    def 打开高亮设置(self):
        对话框 = 设置对话框(self)
        if 对话框.exec_() == QDialog.Accepted:
            结果 = 对话框.获取结果()
            self.高亮器.utf16对齐修复 = 结果.get("utf16_对齐修复", False)
            self.高亮器.rehighlight()
            self.statusBar().showMessage("高亮设置已更新")

    def 打开解释器设置(self):
        对话框 = 解释器设置对话框(self, 项目目录=self.文件树管理.当前文件夹)
        if 对话框.exec_() == QDialog.Accepted:
            设置 = 读取解释器设置()
            self.运行器.设置解释器设置(设置)
            实际解释器 = self.运行器.获取python可执行文件(self.当前文件)
            self.statusBar().showMessage(f"解释器已设置为: {实际解释器}")

    def 应用输出框样式(self):
        """根据颜色配置更新输出框背景和滚动条"""
        默认背景 = self.颜色配置.get("默认背景色", "#ffffff")
        文字色 = self.状态栏文字色()
        self.输出区.setStyleSheet(f"""
            QPlainTextEdit {{
                background-color: {默认背景};
                color: {文字色};
            }}
            QPlainTextEdit QScrollBar:vertical {{
                background: {默认背景};
                width: 14px;
                margin: 0;
            }}
            QPlainTextEdit QScrollBar::handle:vertical {{
                background: #c0c0c0;
                min-height: 30px;
                border-radius: 7px;
                margin: 2px;
            }}
            QPlainTextEdit QScrollBar::handle:vertical:hover {{
                background: #a0a0a0;
            }}
            QPlainTextEdit QScrollBar::handle:vertical:pressed {{
                background: #888;
            }}
            QPlainTextEdit QScrollBar::add-line:vertical, QPlainTextEdit QScrollBar::sub-line:vertical {{
                height: 0;
            }}
            QPlainTextEdit QScrollBar::add-page:vertical, QPlainTextEdit QScrollBar::sub-page:vertical {{
                background: none;
            }}
        """)

    def 状态栏文字色(self):
        """根据背景色深浅返回合适的文字色"""
        默认背景 = self.颜色配置.get("默认背景色", "#ffffff")
        # 优先使用普通代码文字色
        普通文字色 = self.颜色配置.get("普通代码", {}).get("文字色", "")
        if 普通文字色 and 普通文字色 != "black":
            return 普通文字色
        默认背景 = 默认背景.lstrip("#")
        if not 默认背景:
            return "white"
        try:
            r, g, b = int(默认背景[0:2], 16), int(默认背景[2:4], 16), int(默认背景[4:6], 16)
            亮度 = (r * 299 + g * 587 + b * 114) / 1000
            return "white" if 亮度 < 128 else "black"
        except (ValueError, IndexError):
            return "black"


if __name__ == "__main__":
    # 标准流已在模块顶部重定向，此处直接启动界面
    应用 = QApplication(sys.argv)
    图标路径 = os.path.join(获取资源目录(), "AEpy.ico")
    if os.path.isfile(图标路径):
        应用.setWindowIcon(QIcon(图标路径))
    窗口 = 主窗口()

    # 解析命令行参数：--file 路径 或 --folder 路径
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--file" and i + 1 < len(args):
            路径 = args[i + 1]
            if os.path.isfile(路径):
                QTimer.singleShot(100, lambda p=路径: 窗口.打开文件(p))
            i += 2
        elif args[i] == "--folder" and i + 1 < len(args):
            路径 = args[i + 1]
            if os.path.isdir(路径):
                QTimer.singleShot(100, lambda p=路径: 窗口.打开文件夹(p, 询问保存=False))
            i += 2
        else:
            # 无标志时：目录视为文件夹，.py/.pyw 视为文件
            路径 = args[i]
            if os.path.isfile(路径) and 路径.lower().endswith((".py", ".pyw")):
                QTimer.singleShot(100, lambda p=路径: 窗口.打开文件(p))
            elif os.path.isdir(路径):
                QTimer.singleShot(100, lambda p=路径: 窗口.打开文件夹(p, 询问保存=False))
            i += 1

    窗口.show()
    sys.exit(应用.exec_())
