# 调用解释器.py - 颜色跟随主题，自动滚动仅在底部时启用
# 解释器优先级：项目解释器（.pe 第 2 行）→ 虚拟环境 → 自定义路径 → 系统默认 python
# .exe/.bat/.cmd 作为程序入口时直接运行，不经过解释器
import os
import shlex
import sys
from PyQt5.QtCore import QProcess, QFileInfo, QProcessEnvironment, QTimer
from PyQt5.QtGui import QTextCursor, QTextCharFormat, QColor
from PyQt5.QtWidgets import QMessageBox

from config.解释器配置 import (
    读取解释器设置, 解析解释器, 解析解释器命令, 系统默认python, 是python解释器
)
from config.项目入口 import 获取项目解释器, 项目解释器信息


# 可直接运行的程序扩展名（不调用解释器）
可执行扩展名 = (".exe", ".bat", ".cmd")


def 是可直接执行文件(路径):
    """判断是否是可以直接运行的二进制/脚本程序（.exe、.bat、.cmd）"""
    return os.path.splitext(路径)[1].lower() in 可执行扩展名


def _命令引号(文本):
    """cmd.exe 命令行中的参数引号（参数已由 shlex 去掉引号）"""
    return '"' + str(文本).replace('"', "") + '"'


class 脚本运行器:
    def __init__(self, 输出区, 状态栏, 颜色配置=None, 解释器设置=None):
        self.输出区 = 输出区
        self.状态栏 = 状态栏
        self.颜色配置 = 颜色配置 or {}
        self.解释器设置 = 解释器设置 if 解释器设置 is not None else 读取解释器设置()
        # 当前打开的项目目录，用于查找虚拟环境
        self.项目目录 = None
        self.进程 = QProcess()
        self.进程.readyReadStandardOutput.connect(lambda: self.读取标准输出())
        self.进程.readyReadStandardError.connect(lambda: self.读取标准错误())
        self.进程.finished.connect(lambda exit_code, exit_status: self.进程结束(exit_code, exit_status))

        self.输出区.document().setMaximumBlockCount(5000)
        self.输出区.setUndoRedoEnabled(False)

        # 分离两个缓冲区
        self.输出缓冲区_stdout = ""
        self.输出缓冲区_stderr = ""
        self.刷新定时器 = QTimer()
        self.刷新定时器.timeout.connect(lambda: self._刷新缓冲区())
        self.刷新定时器.setInterval(50)

    def _普通文字色(self):
        """从颜色配置获取普通代码文字色"""
        色值 = self.颜色配置.get("普通代码", {}).get("文字色", "black")
        return 色值

    def _系统输出消息色(self):
        """输出框里 AEpy 自己的消息（[AEpy] 开头的提示行）的颜色，默认绿色。

        与代码编辑框的颜色区分开：程序自身的输出仍然跟随“普通代码”文字色。
        """
        return self.颜色配置.get("系统输出消息", {}).get("文字色", "#2E7D32")

    def _解释器输出色(self):
        """解释器的错误/日志输出（stderr、非 0 退出码提示等）的颜色，默认红色"""
        return self.颜色配置.get("解释器输出", {}).get("文字色", "#C62828")

    def _是否在底部(self):
        """检查输出框滚动条是否在最底部（误差 2px）"""
        scrollbar = self.输出区.verticalScrollBar()
        if not scrollbar.isVisible():
            return True
        return scrollbar.value() >= scrollbar.maximum() - 2

    def 更新颜色配置(self, 颜色配置):
        self.颜色配置 = 颜色配置

    def 设置解释器设置(self, 设置):
        """更新解释器设置（来自解释器设置对话框）"""
        self.解释器设置 = 设置 or 读取解释器设置()

    def 设置项目目录(self, 目录):
        """记录当前打开的项目目录，用于查找虚拟环境"""
        self.项目目录 = 目录

    def 解析起始目录(self, 脚本路径=None):
        """虚拟环境查找的起始目录：优先项目目录，其次脚本所在目录"""
        if self.项目目录:
            return self.项目目录
        if 脚本路径:
            return os.path.dirname(os.path.abspath(脚本路径))
        return None

    def 适用项目目录(self, 脚本路径=None):
        """项目解释器只对项目内的脚本生效；未打开项目或脚本在项目外时返回 None"""
        if not self.项目目录:
            return None
        if not 脚本路径:
            return self.项目目录
        try:
            项目绝对 = os.path.abspath(self.项目目录)
            脚本绝对 = os.path.abspath(脚本路径)
            if os.path.commonpath([项目绝对, 脚本绝对]) == 项目绝对:
                return self.项目目录
        except ValueError:
            pass
        return None

    def 解析实际解释器(self, 脚本路径=None):
        """解析最终使用的 Python 解释器，返回 (解释器路径, 备注列表)

        顺序：项目解释器（.pe 第 2 行，有效时）→ 虚拟环境 → 自定义路径 → 系统默认 python。
        备注列表用于在输出区提示“项目解释器无效”等回退情况。
        """
        备注 = []
        目录 = self.适用项目目录(脚本路径)
        if 目录:
            原文, 绝对, 有效 = 项目解释器信息(目录)
            if 有效:
                return 绝对, 备注
            if 原文:
                备注.append(f"[AEpy]项目解释器无效（{原文}），已改用解释器设置的原逻辑")

        路径 = 解析解释器(self.解释器设置, self.解析起始目录(脚本路径))
        if 路径:
            命令 = 解析解释器命令(路径)
            if os.path.dirname(命令) and not os.path.isfile(命令):
                备注.append(f"[AEpy]解释器路径无效（{命令}），已改用系统默认 python")
            else:
                return 命令, 备注

        # 注意：不能回退到 sys.executable —— 通过 AEpy.exe 启动时它是 AEpy
        # 安装目录下自带虚拟环境的解释器，而不是系统默认 python。
        return (系统默认python() or sys.executable), 备注

    def 获取python可执行文件(self, 脚本路径=None):
        """返回要使用的 Python 可执行文件路径"""
        return self.解析实际解释器(脚本路径)[0]

    def _注入虚拟环境变量(self, 环境, python可执行文件):
        """使用虚拟环境解释器时同步 VIRTUAL_ENV 与 PATH，便于子进程正确继承"""
        if not python可执行文件:
            return
        脚本目录 = os.path.dirname(os.path.abspath(python可执行文件))
        虚拟环境目录 = os.path.dirname(脚本目录)
        if os.path.basename(脚本目录).lower() != "scripts":
            return
        if not os.path.isfile(os.path.join(虚拟环境目录, "pyvenv.cfg")):
            return
        环境.insert("VIRTUAL_ENV", 虚拟环境目录)
        原PATH = 环境.value("PATH") or ""
        环境.insert("PATH", 脚本目录 + os.pathsep + 原PATH)

    def 运行(self, 脚本路径, 参数字符串):
        if self.进程.state() != QProcess.NotRunning:
            self.进程.kill()
            self.进程.waitForFinished(1000)

        self.输出区.clear()
        self.输出缓冲区_stdout = ""
        self.输出缓冲区_stderr = ""
        self.刷新定时器.start()

        参数列表 = []
        if 参数字符串.strip():
            try:
                参数列表 = shlex.split(参数字符串)
            except ValueError:
                self.刷新定时器.stop()
                QMessageBox.warning(None, "参数错误", "参数格式错误")
                return False

        self.进程.setWorkingDirectory(QFileInfo(脚本路径).absolutePath())

        环境 = QProcessEnvironment.systemEnvironment()
        环境.insert("PYTHONIOENCODING", "utf-8")

        if 是可直接执行文件(脚本路径):
            备注 = []
            self._输出编码 = None  # 直接运行的程序：输出按本机代码页回退解码
            self.进程.setProcessEnvironment(环境)
            self._准备程序启动(脚本路径, 参数列表)
            启动提示 = f"[AEpy]运行程序: {脚本路径}"
        else:
            python可执行文件, 备注 = self.解析实际解释器(脚本路径)
            self._输出编码 = "utf-8"  # 脚本设置 PYTHONIOENCODING=utf-8，输出一定是 UTF-8
            self._注入虚拟环境变量(环境, python可执行文件)
            self.进程.setProcessEnvironment(环境)
            self._准备解释器启动(python可执行文件, 脚本路径, 参数列表)
            启动提示 = f"[AEpy]解释器: {python可执行文件}"

        self.进程.start()
        if not self.进程.waitForStarted(3000):
            self.刷新定时器.stop()
            QMessageBox.warning(None, "运行错误", "无法启动进程")
            return False

        self._插入系统输出消息(启动提示)
        for 提示 in 备注:
            self._插入系统输出消息(提示)
        self._插入系统输出消息("[AEpy]程序开始运行")
        self.状态栏.showMessage("正在运行脚本...")
        return True

    # ---------- 启动命令组装 ----------
    def _设置参数列表(self, 参数列表):
        """用 Qt 的参数列表启动（Qt 会自动处理引号）"""
        if hasattr(self.进程, "setNativeArguments"):
            self.进程.setNativeArguments("")
        self.进程.setArguments(list(参数列表 or []))

    def _准备解释器启动(self, python可执行文件, 脚本路径, 参数列表):
        """用解释器运行脚本：python 类解释器加 -u 保证输出不缓冲"""
        if 是python解释器(python可执行文件):
            命令列表 = ["-u", 脚本路径]
        else:
            命令列表 = [脚本路径]
        命令列表 += list(参数列表 or [])
        self.进程.setProgram(python可执行文件)
        self._设置参数列表(命令列表)

    def _准备程序启动(self, 程序路径, 参数列表):
        """直接运行 .exe/.bat/.cmd，不调用解释器"""
        扩展 = os.path.splitext(程序路径)[1].lower()
        if 扩展 in (".bat", ".cmd"):
            # .bat/.cmd 必须交给 cmd.exe 解释执行；用原生命令行保证带空格的路径正确
            命令 = f'cmd.exe /c ""{程序路径}"'
            if 参数列表:
                命令 += " " + " ".join(_命令引号(参数) for 参数 in 参数列表)
            命令 += '"'
            self.进程.setProgram("cmd.exe")
            原生参数 = 命令[len("cmd.exe "):]
            self.进程.setArguments([])
            if hasattr(self.进程, "setNativeArguments"):
                self.进程.setNativeArguments(原生参数)
            else:  # 非 Windows 平台的兜底
                self.进程.setArguments(["/c", 程序路径] + list(参数列表 or []))
        else:
            self.进程.setProgram(程序路径)
            self._设置参数列表(参数列表)

    def 停止(self):
        if self.进程.state() != QProcess.NotRunning:
            self.进程.kill()
            self.进程.waitForFinished(1000)
        self.刷新定时器.stop()
        self._刷新缓冲区()
        self.状态栏.showMessage("已停止")

    def 发送输入(self, 文本):
        if self.进程.state() == QProcess.Running:
            self.进程.write((文本 + '\n').encode('utf-8'))
            self._插入文本(文本 + '\n', self._普通文字色())
            self.输出区.ensureCursorVisible()

    def 读取标准输出(self):
        数据 = self.进程.readAllStandardOutput()
        文本 = self._解码输出(数据)
        self.输出缓冲区_stdout += 文本

    def 读取标准错误(self):
        数据 = self.进程.readAllStandardError()
        文本 = self._解码输出(数据)
        self.输出缓冲区_stderr += 文本

    def _解码输出(self, 数据):
        """解码子进程输出。

        Python 脚本（设置了 PYTHONIOENCODING=utf-8）按 UTF-8 解码；
        直接运行的程序（.exe/.bat/.cmd）先试 UTF-8，失败再按本机 ANSI 代码页解码，
        避免中文输出乱码。
        """
        字节 = bytes(数据)
        编码 = getattr(self, "_输出编码", "utf-8")
        if 编码:
            return 字节.decode(编码, errors="replace")
        try:
            return 字节.decode("utf-8")
        except UnicodeDecodeError:
            try:
                return 字节.decode("mbcs", errors="replace")
            except (LookupError, UnicodeDecodeError):
                return 字节.decode("utf-8", errors="replace")

    def 进程结束(self, 退出码, exit_status):
        self.刷新定时器.stop()
        self._刷新缓冲区()
        self.状态栏.showMessage("进程已结束")

        # 退出消息始终用“系统输出消息”色（默认绿色）：
        # “解释器输出”色可能被用户自定义成别的颜色，不适合承载退出提示。
        # 失败由退出码文字本身和 stderr 的“解释器输出”色体现。
        if exit_status == QProcess.NormalExit:
            self._插入文本(f"\n[AEpy]程序运行结束 (退出码: {退出码})\n", self._系统输出消息色())
        else:
            self._插入文本(f"\n[AEpy]程序异常结束 (退出码: {退出码})\n", self._系统输出消息色())

    def _插入系统输出消息(self, 文本):
        """插入 AEpy 自己的提示行（颜色可在 样式设置 → 语法 → 系统输出消息 中调整）"""
        self._插入文本(文本 + "\n", self._系统输出消息色())

    def _插入文本(self, 文本, 颜色):
        self.输出区.moveCursor(QTextCursor.End)
        格式 = QTextCharFormat()
        格式.setForeground(QColor(颜色))
        self.输出区.textCursor().insertText(文本, 格式)

    def _刷新缓冲区(self):
        """定时器触发：将缓冲区内容写入输出区"""
        if self.输出缓冲区_stdout:
            # 程序自身的输出跟随代码编辑框的“普通代码”文字色
            self._插入文本(self.输出缓冲区_stdout, self._普通文字色())
            self.输出缓冲区_stdout = ""
        if self.输出缓冲区_stderr:
            # 解释器的错误/日志输出（stderr）用“解释器输出”色，默认红色
            self._插入文本(self.输出缓冲区_stderr, self._解释器输出色())
            self.输出缓冲区_stderr = ""
        if self._是否在底部():
            self.输出区.moveCursor(QTextCursor.End)
            self.输出区.ensureCursorVisible()
