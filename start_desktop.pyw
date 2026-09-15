"""Windowed launcher, including visible startup diagnostics."""
from pathlib import Path
import os
import sys
import traceback

project = Path(__file__).resolve().parent
os.chdir(project)
sys.path.insert(0, str(project / "src"))

try:
    from minidbms.desktop.app import main
    main()
except Exception:
    import tkinter as tk
    from tkinter import messagebox
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror("MiniDBMS 启动失败", traceback.format_exc())
    root.destroy()
