# 项目入口.py - .pe 文件读写（第1行：程序入口；第2行：项目解释器）
# .pe 是项目根目录下的标记文件，内容固定两行：
#   第 1 行：程序入口（相对项目根目录的路径；项目外的文件用绝对路径）
#   第 2 行：项目解释器（可选。留空或路径无效时，按“解释器设置”的原逻辑解析）
import os

入口文件名 = ".pe"


def 入口文件路径(项目目录):
    """返回项目目录下 .pe 文件的完整路径"""
    if not 项目目录:
        return ""
    return os.path.join(项目目录, 入口文件名)


def 读取入口信息(项目目录):
    """读取 .pe，返回 (入口路径, 解释器路径)，两者都是文件中的原文（可能为 ""）"""
    if not 项目目录:
        return "", ""
    try:
        with open(入口文件路径(项目目录), "r", encoding="utf-8") as f:
            行列表 = [行.strip() for 行 in f.read().splitlines()]
    except OSError:
        return "", ""
    入口 = 行列表[0] if len(行列表) > 0 else ""
    解释器 = 行列表[1] if len(行列表) > 1 else ""
    return 入口, 解释器


def 写入入口信息(项目目录, 入口路径=None, 解释器路径=None):
    """把入口与项目解释器写回 .pe（始终保持两行，空值写空行）"""
    if not 项目目录:
        raise ValueError("项目目录为空")
    入口 = (入口路径 or "").strip()
    解释器 = (解释器路径 or "").strip()
    os.makedirs(项目目录, exist_ok=True)
    路径 = 入口文件路径(项目目录)
    with open(路径, "w", encoding="utf-8") as f:
        f.write(f"{入口}\n{解释器}\n")
    return 路径


def 转绝对路径(项目目录, 路径):
    """相对路径按项目目录展开，绝对路径原样返回；空值返回 None"""
    if not 路径:
        return None
    路径 = 路径.strip()
    if os.path.isabs(路径):
        return os.path.normpath(路径)
    if not 项目目录:
        return os.path.normpath(路径)
    return os.path.normpath(os.path.join(项目目录, 路径))


def 转存储路径(项目目录, 文件路径):
    """项目内的文件存相对路径（便于项目整体搬运），项目外的文件存绝对路径"""
    绝对 = os.path.abspath(文件路径)
    if 项目目录:
        项目绝对 = os.path.abspath(项目目录)
        try:
            if os.path.commonpath([绝对, 项目绝对]) == 项目绝对:
                return os.path.relpath(绝对, 项目绝对)
        except ValueError:
            pass
    return 绝对


def 解析入口路径(项目目录):
    """返回入口文件的绝对路径；未设置返回 None（不校验文件是否存在）"""
    入口, _ = 读取入口信息(项目目录)
    return 转绝对路径(项目目录, 入口)


def 项目解释器信息(项目目录):
    """返回 (配置原文, 绝对路径, 是否有效)

    未配置时配置原文为 ""、绝对路径为 None；路径无效（文件不存在）时有效为 False。
    """
    _, 解释器 = 读取入口信息(项目目录)
    绝对 = 转绝对路径(项目目录, 解释器)
    if 绝对 and os.path.isfile(绝对):
        return 解释器, 绝对, True
    return 解释器, 绝对, False


def 获取项目解释器(项目目录):
    """项目解释器有效时返回其绝对路径，否则返回 None"""
    _, 绝对, 有效 = 项目解释器信息(项目目录)
    return 绝对 if 有效 else None
