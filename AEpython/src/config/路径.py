# 路径.py - 统一计算项目内各类目录，避免各模块各自拼接 path
# 目录结构：
#   AEpython/   项目根目录
#     src/      源码（本文件位于 src/config/路径.py）
#     config/   运行时配置（*.json），可写
#     assets/   图标、图片等静态资源
#     tools/    开发辅助脚本
#     .venv/    虚拟环境
import json
import os

# src/config/路径.py -> src/config -> src -> 项目根目录
_本文件目录 = os.path.dirname(os.path.abspath(__file__))
_源码目录 = os.path.dirname(_本文件目录)
_项目根目录 = os.path.dirname(_源码目录)


def 获取项目根目录():
    """项目根目录（AEpython/）"""
    return _项目根目录


def 获取源码目录():
    """源码目录（AEpython/src/）"""
    return _源码目录


def 获取配置目录():
    """运行时配置目录（AEpython/config/），不存在时自动创建"""
    目录 = os.path.join(_项目根目录, "config")
    os.makedirs(目录, exist_ok=True)
    return 目录


def 获取资源目录():
    """静态资源目录（AEpython/assets/）"""
    return os.path.join(_项目根目录, "assets")


def 配置文件路径(文件名):
    """返回 config/ 下某个配置文件的完整路径"""
    return os.path.join(获取配置目录(), 文件名)


def 读取JSON(文件名, 默认=None):
    """读取 config/ 下的 JSON 文件，失败时返回默认值"""
    try:
        with open(配置文件路径(文件名), "r", encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return 默认


def 保存JSON(文件名, 数据):
    """把数据写入 config/ 下的 JSON 文件"""
    os.makedirs(获取配置目录(), exist_ok=True)
    with open(配置文件路径(文件名), "w", encoding="utf-8") as f:
        json.dump(数据, f, indent=2, ensure_ascii=False)
