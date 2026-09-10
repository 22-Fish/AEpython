# 解释器配置.py - Python 解释器路径与虚拟环境设置（配置层，不依赖 GUI）
import os
import shutil

from config.路径 import 读取JSON, 保存JSON
from 解释器.虚拟环境 import 查找虚拟环境, 取解释器

配置文件 = "interpreter.json"

# 默认设置：默认勾选“使用虚拟环境”
默认设置 = {
    "python_path": "",
    "use_venv": True,
}


def 读取解释器设置():
    """读取解释器设置，返回 {"python_path": str, "use_venv": bool}"""
    设置 = dict(默认设置)
    数据 = 读取JSON(配置文件, {})
    if isinstance(数据, dict):
        设置["python_path"] = 数据.get("python_path", "") or ""
        设置["use_venv"] = bool(数据.get("use_venv", True))
    return 设置


def 保存解释器设置(设置):
    """保存解释器设置字典"""
    数据 = {
        "python_path": (设置.get("python_path", "") or "").strip(),
        "use_venv": bool(设置.get("use_venv", True)),
    }
    保存JSON(配置文件, 数据)


def 解析解释器(设置=None, 起始目录=None):
    """根据设置解析实际使用的 Python 解释器路径。

    规则：
      1. 勾选“使用虚拟环境”时，若项目目录下存在虚拟环境，使用其解释器；
      2. 否则使用自定义解释器路径；
      3. 都没有时返回空字符串，由调用方回退到当前解释器。
    """
    if 设置 is None:
        设置 = 读取解释器设置()
    if 设置.get("use_venv", True):
        虚拟环境解释器 = 取解释器(查找虚拟环境(起始目录))
        if 虚拟环境解释器:
            return 虚拟环境解释器
    return 设置.get("python_path", "") or ""


# 系统默认解释器的候选命令名，按优先级排列（python.exe 优先，保证能拿到输出）
系统解释器候选 = ("python.exe", "python", "python3.exe", "python3", "py.exe", "py")


def 系统默认python():
    """返回系统默认的 Python 解释器路径，找不到返回 ""。

    注意：这里刻意不使用 sys.executable。通过 AEpy.exe 启动时，当前进程用的是
    AEpy 安装目录下自带虚拟环境里的解释器，它不是“系统默认 python”，
    直接拿它运行用户脚本会造成“AEpy 总是用本体虚拟环境”的问题。
    """
    for 名称 in 系统解释器候选:
        路径 = shutil.which(名称)
        if 路径:
            return 路径
    return ""


def 解析解释器命令(路径):
    """把配置里的解释器路径规整成可直接启动的路径。

    - 绝对/相对文件路径：原样返回（存在的文件）；
    - 裸命令名（如 "python"、"py"）：在 PATH 中解析成绝对路径；
    - 解析不到时原样返回，交给 QProcess 再试一次 PATH 查找。
    """
    if not 路径:
        return ""
    路径 = 路径.strip()
    if os.path.isfile(路径):
        return 路径
    if os.path.dirname(路径):
        # 带目录但不存在的路径：交由调用方处理（说明路径无效）
        return 路径
    解析结果 = shutil.which(路径)
    return 解析结果 or 路径


def 是python解释器(路径):
    """判断某个可执行文件是否（类）Python 解释器，用于决定要不要加 -u 参数"""
    if not 路径:
        return False
    名称 = os.path.basename(路径).lower()
    return "python" in 名称 or 名称 in ("py.exe", "pyw.exe", "py")
