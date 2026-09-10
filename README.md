# AEpy — 简易 Python 编辑器
- **⚠含AI生成内容**

**AEpy** 是一个基于 PyQt5 的轻量级 Python 代码编辑器
**技术栈：** Python 3.8+，PyQt5，QProcess，MinGW-w64（编译启动器）

## 目录结构

```
AEpython/
├─ AEpy.exe              编译生成的启动器（无控制台，见下）
├─ launcher.c            启动器源码（C）
├─ launcher.rc           启动器资源（图标 / 清单 / 版本信息）
├─ app.manifest          应用程序清单
├─ build_launcher.bat    编译启动器
├─ setup_venv.bat        创建虚拟环境并安装依赖
├─ requirements.txt      Python 依赖
├─ .pe                   项目入口标记（第 1 行入口，第 2 行项目解释器）
├─ assets/               图标等静态资源（AEpy.svg 为图标源文件）
├─ config/               运行时配置（JSON，可写）
├─ src/                  源码
│   ├─ main.pyw          程序入口
│   ├─ GUI/              界面、编辑器、对话框、文件树
│   ├─ 解释器/            脚本进程调用、虚拟环境与解释器解析
│   └─ config/           路径与配置读写
├─ tools/                开发辅助脚本
└─ .venv/                虚拟环境
```


## 启动器（AEpy.exe）

`launcher.c` 用 C 编写，编译出的 `AEpy.exe` **不带控制台窗口**，行为如下：

1. 以自身所在目录为项目根目录，优先使用虚拟环境里的解释器
   （`.venv\Scripts\pythonw.exe`，其次 `venv`、`env`），找不到再回退到 PATH 上的 Python；
2. 用 `CreateProcessW` + `DETACHED_PROCESS` / `CREATE_NO_WINDOW` **分离式**启动
   `src\main.pyw`：不继承句柄、不共享控制台；
3. 启动成功后立刻关闭进程句柄并退出，**不等待**编辑器进程结束，
   因此关闭启动器不会影响已经打开的 AEpy。

命令行参数会原样转发给 `src\main.pyw`（支持 `--file 路径`、`--folder 路径`）。

**重新编译：**

```
build_launcher.bat
```

需要 MinGW-w64 的 `gcc` 与 `windres` 在 PATH 中。等价的命令是：

```
windres launcher.rc -O coff -o launcher_res.o
gcc -O2 -s -mwindows -o AEpy.exe launcher.c launcher_res.o
```

## 程序入口与项目解释器（.pe）

项目根目录下的 `.pe` 文件是程序入口标记，固定两行：

```
src\main.pyw                      ← 第 1 行：程序入口（相对项目根目录）
.venv\Scripts\python.exe          ← 第 2 行：项目解释器（可留空）
```

- **设置入口**：在左侧文件树中右键文件 → “设置为程序入口”，
  或打开文件后点击状态栏右侧的“⚙ 设置入口”。
  入口为 `.py`/`.pyw` 时用解释器运行；
  入口为 `.exe`/`.bat`/`.cmd` 时**直接运行程序，不调用解释器**
  （`.bat`/`.cmd` 交给 `cmd.exe` 执行；运行参数、启动目录、输出捕获照常生效）。
- **项目解释器**：右键 `.exe` 文件 → “设置为项目解释器”，
  路径写入 `.pe` 第 2 行（项目内的文件存相对路径，项目外的存绝对路径）；
  右键菜单中的“清除项目解释器”可清空该行。
  第 2 行为空或路径无效时，按下面的“解释器设置”照常解析。

## 解释器设置

菜单「设置 → 解释器设置」中：

- **使用虚拟环境**（默认勾选）：勾选后，若项目目录下存在虚拟环境
  （`.venv`、`venv`、`env`），就用其中的 Python 解释器运行脚本；
  查找会从项目目录（未打开项目时从脚本所在目录）逐级向上进行。
- **Python 解释器路径**：未勾选虚拟环境、或没找到虚拟环境时使用；
  留空则回退到系统默认 `python`。

运行脚本时的解释器优先级为：

1. **项目解释器**（`.pe` 第 2 行，路径有效时）；
2. 项目虚拟环境（勾选了“使用虚拟环境”且找到时）；
3. 自定义 **Python 解释器路径**；
4. **系统默认 `python`**（从 `PATH` 查找）。

> 注意：第 4 步刻意不使用 AEpy 自身的解释器。通过 `AEpy.exe` 启动时，
> 编辑器进程跑在 AEpy 安装目录自带虚拟环境（`.venv`）里，

配置保存在 `config/interpreter.json`。

### 输出框的颜色（样式设置 → 语法）

输出框里的文字分两类：

- **程序自身的输出**（脚本的 `print` 等）跟随代码编辑框的
  **普通代码**文字色，不单独设置；
- **解释器输出**：解释器的错误与日志（脚本写到 stderr 的报错、回溯等），
  默认红色 `#C62828`；
- **系统输出消息**：AEpy 自己的提示行（`[AEpy]解释器: …`、
  `[AEpy]程序开始运行`、`[AEpy]程序运行结束 (退出码: 0)` 等），默认绿色 `#2E7D32`。

这两项都在「样式 → 语法」的颜色列表末尾，只调文字色，与代码编辑框的颜色区分开。
退出提示（`程序运行结束`、`程序异常结束`）始终用系统输出消息色，
不受“解释器输出”颜色影响；失败由退出码文字本身和 stderr 的颜色体现。
颜色保存在 `config/colors.json`，旧配置会自动补齐或迁移键名。

## 开发辅助脚本（tools/）

- `make_icon.py`：把 `assets/AEpy.svg` 渲染成 `AEpy.png` 与多尺寸 `AEpy.ico`；
- `embed_files.py`：把项目文件打包成 C 头文件，供安装器使用；
- `debug_highlight.py`：逐字符打印语法高亮的格式分段，排查偏移问题；
- `test_dialog.py`：单独运行设置对话框。

