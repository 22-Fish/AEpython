# 虚拟环境.py - 检测项目目录下的 Python 虚拟环境
import os

# 常见的虚拟环境目录名，按优先级排列
候选目录名 = (".venv", "venv", "env")
解释器子路径 = os.path.join("Scripts", "python.exe")


def 取解释器(虚拟环境目录):
    """返回虚拟环境中的 python.exe 路径，不存在则返回 None"""
    if not 虚拟环境目录:
        return None
    路径 = os.path.join(虚拟环境目录, 解释器子路径)
    return 路径 if os.path.isfile(路径) else None


def 是虚拟环境(目录):
    """判断目录是否是有效的虚拟环境"""
    if not 目录 or not os.path.isdir(目录):
        return False
    if os.path.isfile(os.path.join(目录, "pyvenv.cfg")):
        return True
    return os.path.isfile(os.path.join(目录, 解释器子路径))


def 查找虚拟环境(起始目录, 向上层级=6):
    """从起始目录开始逐级向上查找虚拟环境，返回虚拟环境目录，找不到返回 None。
    起始目录可以是文件或目录。
    """
    if not 起始目录:
        return None
    当前 = os.path.abspath(起始目录)
    if os.path.isfile(当前):
        当前 = os.path.dirname(当前)
    for _ in range(max(1, 向上层级)):
        for 名称 in 候选目录名:
            候选 = os.path.join(当前, 名称)
            if 是虚拟环境(候选):
                return 候选
        父目录 = os.path.dirname(当前)
        if 父目录 == 当前:  # 已到盘符根目录
            break
        当前 = 父目录
    return None


def 查找解释器(起始目录, 向上层级=6):
    """查找并返回虚拟环境中的 python.exe，找不到返回 None"""
    return 取解释器(查找虚拟环境(起始目录, 向上层级))
