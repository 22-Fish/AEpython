import sys, traceback
import os

# 开发脚本位于 tools/，需要把 src 目录加入 sys.path 才能导入项目模块
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src"))

try:
    from PyQt5.QtWidgets import QApplication, QMainWindow
    from PyQt5.QtCore import QTimer
    from GUI.高亮设置 import 设置对话框, 读取设置

    print("imports OK", flush=True)
    print("settings:", 读取设置(), flush=True)

    app = QApplication(sys.argv)
    win = QMainWindow()
    win.resize(600, 400)
    win.show()

    def test():
        try:
            print("creating dialog...", flush=True)
            dlg = 设置对话框(win)
            print("created, exec...", flush=True)
            result = dlg.exec_()
            print(f"result={result}", flush=True)
        except Exception as e:
            traceback.print_exc()
        sys.exit(0)

    QTimer.singleShot(300, test)
    sys.exit(app.exec_())
except Exception as e:
    traceback.print_exc()
    sys.exit(1)
