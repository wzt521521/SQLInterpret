"""Tk desktop interface; all widget operations run on the main thread."""
from pathlib import Path
from queue import Empty, Queue
from threading import Thread
import re
import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from uuid import uuid4

from .service import execute_sql
from minidbms.cli.main import split_complete_sql


class Workbench:
    def __init__(self, root, path):
        self.root, self.path = root, Path(path)
        self.busy = False
        self.queue = Queue()
        self.last_sql = None
        self.location = None
        self.internal_change = False
        root.title("MiniDBMS · 本地 SQL 工作台")
        root.geometry("1320x820")
        root.minsize(900, 620)
        style = ttk.Style(root)
        style.theme_use("clam")
        root.configure(background="#252526")
        style.configure("TFrame", background="#252526")
        style.configure("TLabel", background="#252526", foreground="#cccccc", font=("Microsoft YaHei UI", 10))
        style.configure("TButton", padding=(10, 5), background="#333333", foreground="#dddddd", borderwidth=0)
        style.map("TButton", background=[("active", "#444444")])
        style.map("TButton", foreground=[("disabled", "#777777")])
        style.configure("TLabelframe", background="#252526", foreground="#9cdcfe")
        style.configure("TLabelframe.Label", background="#252526", foreground="#9cdcfe")
        style.configure("TNotebook", background="#252526", borderwidth=0)
        style.configure("TNotebook.Tab", background="#2d2d2d", foreground="#bbbbbb", padding=(12, 5))
        style.map("TNotebook.Tab", background=[("selected", "#1e1e1e")], foreground=[("selected", "#ffffff")])
        style.configure("Treeview", background="#1e1e1e", fieldbackground="#1e1e1e", foreground="#cccccc", rowheight=27, borderwidth=0)
        style.map("Treeview", background=[("selected", "#264f78")], foreground=[("selected", "#ffffff")])
        style.configure("Treeview.Heading", background="#2d2d2d", foreground="#cccccc", relief="flat")
        style.configure("Vertical.TScrollbar", background="#424242", troughcolor="#1e1e1e", bordercolor="#1e1e1e", arrowcolor="#aaaaaa")
        style.configure("Horizontal.TScrollbar", background="#424242", troughcolor="#1e1e1e", bordercolor="#1e1e1e", arrowcolor="#aaaaaa")
        style.configure("TPanedwindow", background="#383838")
        style.configure("Run.TButton", background="#0e639c", foreground="#ffffff")
        style.map("Run.TButton", background=[("active", "#1177bb")])
        style.configure("Status.TLabel", background="#007acc", foreground="#ffffff", font=("Microsoft YaHei UI", 9))
        header = ttk.Frame(root, padding=(18, 12, 18, 8))
        header.pack(fill="x")
        ttk.Label(header, text="▣  MiniDBMS", font=("Microsoft YaHei UI", 16, "bold"), foreground="#4fc1ff").pack(side="left")
        ttk.Label(header, text="  SQL 工作台", font=("Microsoft YaHei UI", 13), foreground="#cccccc").pack(side="left")
        self.db_label = ttk.Label(header, text=self.path.name)
        self.db_label.pack(side="right")
        toolbar = ttk.Frame(root, padding=(18, 0, 18, 8))
        toolbar.pack(fill="x")
        self.buttons = []
        for label, command in [("执行 SQL", self.run), ("打开 SQL", self.open_sql), ("保存 SQL", self.save_sql),
                               ("选择数据库", self.choose_db), ("清空编辑器", lambda: self.set_sql("")),
                               ("清空数据库", self.clear_database)]:
            button = ttk.Button(toolbar, text=label, command=command)
            button.pack(side="left", padx=(0, 6))
            self.buttons.append(button)
        self.buttons[0].configure(text="▶  执行 SQL", style="Run.TButton")
        body = ttk.Frame(root)
        body.pack(fill="both", expand=True)
        panes = self.panes = ttk.Panedwindow(body, orient="vertical")
        panes.pack(fill="both", expand=True)
        edit_frame = ttk.Frame(panes)
        panes.add(edit_frame, weight=8)
        tabbar = ttk.Frame(edit_frame)
        tabbar.pack(fill="x")
        self.file_label = tk.Label(tabbar, text="  SQL  查询.sql   ", background="#1e1e1e", foreground="#ffffff",
                                   padx=16, pady=9, font=("Microsoft YaHei UI", 10))
        self.file_label.pack(side="left")
        ttk.Label(tabbar, text="Ctrl+Enter 执行全部 SQL", foreground="#858585", padding=(10, 0)).pack(side="right")
        tk.Frame(edit_frame, height=1, background="#007acc").pack(fill="x")
        edit_content = tk.Frame(edit_frame, background="#1e1e1e")
        edit_content.pack(fill="both", expand=True)
        self.gutter = tk.Canvas(edit_content, width=54, background="#1e1e1e", highlightthickness=0)
        self.gutter.pack(side="left", fill="y")
        self.editor = tk.Text(edit_content, wrap="none", undo=True, font=("Consolas", 12),
                              padx=14, pady=12, height=12, background="#1e1e1e", foreground="#d4d4d4",
                              insertbackground="#ffffff", selectbackground="#264f78", borderwidth=0, highlightthickness=0,
                              spacing1=2, spacing3=2)
        y = ttk.Scrollbar(edit_content, orient="vertical", command=self.editor.yview)
        x = ttk.Scrollbar(edit_content, orient="horizontal", command=self.editor.xview)
        self.editor.configure(yscrollcommand=lambda a,b: (y.set(a,b), self.draw_lines()), xscrollcommand=x.set)
        y.pack(side="right", fill="y")
        x.pack(side="bottom", fill="x")
        self.editor.pack(fill="both", expand=True)
        self.editor.mark_set("pending_start", "1.0")
        self.editor.mark_gravity("pending_start", "left")
        self.editor.tag_configure("output", foreground="#858585")
        self._original_text = self.editor._w + "_original"
        self.editor.tk.call("rename", self.editor._w, self._original_text)
        self._proxy_command = self.editor._w + "_proxy"
        self.editor.tk.createcommand(self._proxy_command, self.text_command)
        # Tk bindings catch Tcl errors for absent selections/empty undo stacks.
        # Return errors through Tcl rather than leaking a Python callback exception
        # out of mainloop, which would terminate the application.
        self.editor.tk.call("proc", self.editor._w, "args",
                            "lassign [" + self._proxy_command + " {*}$args] code value\n"
                            "return -code $code $value")
        self.editor.tag_configure("error_line", background="#3b2525")
        self.editor.tag_configure("error_char", background="#f14c4c", foreground="#ffffff")
        for name, color in [("keyword", "#569cd6"), ("string", "#ce9178"), ("number", "#b5cea8"), ("comment", "#6a9955")]:
            self.editor.tag_configure(name, foreground=color)
        self.editor.tag_raise("error_char")
        self.editor.bind("<Configure>", lambda e: self.draw_lines())
        self.editor.bind("<<Modified>>", self.modified)
        self.editor.bind("<Control-Return>", lambda e: self.run() or "break")
        self.editor.bind("<KeyRelease>", self.position)
        self.editor.bind("<ButtonRelease-1>", self.position)
        menu = tk.Menu(root, tearoff=False)
        for label, event in [("撤销", "<<Undo>>"), ("剪切", "<<Cut>>"), ("复制", "<<Copy>>"), ("粘贴", "<<Paste>>"), ("全选", "<<SelectAll>>")]:
            menu.add_command(label=label, command=lambda ev=event: self.editor.event_generate(ev))
        self.editor.bind("<Button-3>", lambda e: menu.tk_popup(e.x_root, e.y_root))
        output = self.output = ttk.Frame(panes, padding=(14, 5, 14, 0))
        panes.add(output, weight=2)
        panelbar = ttk.Frame(output)
        panelbar.pack(fill="x")
        self.panel_title = ttk.Label(panelbar, text="执行结果", foreground="#ffffff", padding=(0, 2, 20, 5))
        self.panel_title.pack(side="left")
        self.jump = ttk.Button(panelbar, text="定位到错误", command=self.locate, state="disabled")
        self.jump.pack(side="right")
        self.diagnostic = tk.Text(output, height=1, wrap="word", font=("Microsoft YaHei UI", 9),
                                  background="#2d2d2d", foreground="#cccccc", relief="flat", state="disabled")
        self.diagnostic.pack(fill="x")
        statusbar = tk.Frame(root, background="#007acc")
        statusbar.pack(fill="x")
        self.status = ttk.Label(statusbar, text="就绪  ·  每条 SQL 以分号结束", padding=(12, 3), style="Status.TLabel")
        self.status.pack(side="left")
        self.stats = ttk.Label(statusbar, text="UTF-8    SQL", padding=(12, 3), style="Status.TLabel")
        self.stats.pack(side="right")
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(80, self.poll)
        self.set_sql("")
        self.split_initialized = False
        panes.bind("<Configure>", self.initial_split)

    def initial_split(self, event=None):
        if not self.split_initialized and self.panes.winfo_height() > 100:
            self.split_initialized = True
            self.root.after_idle(lambda: self.panes.sashpos(0, int(self.panes.winfo_height() * .75)))

    def source_index(self, text, offset):
        prefix = text[:offset]
        line = prefix.count("\n") + 1
        units = int(self.root.tk.call("string", "length", prefix.rsplit("\n", 1)[-1]))
        return f"{line}.{units}"

    def draw_lines(self):
        # Use the editor's actual visible line boxes, including font and scroll offsets.
        self.gutter.delete("all")
        index = self.editor.index("@0,0")
        while True:
            box = self.editor.dlineinfo(index)
            if box is None:
                break
            line = index.split(".")[0]
            bad = bool(self.editor.tag_nextrange("error_line", f"{line}.0", f"{line}.end+1c"))
            self.gutter.create_text(43, box[1], anchor="ne", text=line, font=("Consolas", 12),
                                    fill="#f48771" if bad else "#858585")
            index = self.editor.index(f"{index} +1line linestart")

    def color_sql(self):
        text = self.editor.get("1.0", "end-1c")
        for tag in ("keyword", "string", "number", "comment"):
            self.editor.tag_remove(tag, "1.0", "end")
        pattern = r"(?P<comment>--[^\n]*|/\*[\s\S]*?(?:\*/|$))|(?P<string>'(?:''|[^'])*(?:'|$))|(?P<number>\b\d+\b)|(?P<keyword>\b(?:SELECT|FROM|WHERE|CREATE|TABLE|INSERT|INTO|VALUES|DELETE|INT|VARCHAR|AND|OR|NOT|TRUE|FALSE)\b)"
        # Convert Python offsets to Tcl indices from the same text, preserving emoji positions.
        for match in re.finditer(pattern, text, re.IGNORECASE):
            self.editor.tag_add(match.lastgroup, self.source_index(text, match.start()), self.source_index(text, match.end()))
        self.editor.tag_remove("output", "pending_start", "end")
        self.editor.tag_raise("output")
        self.editor.tag_raise("error_char")
        self.draw_lines()

    def position(self, event=None):
        line, column = self.editor.index("insert").split(".")
        prefix = self.editor.get(f"{line}.0", "insert")
        if not self.busy:
            self.status.configure(text=f"Ln {line}, Col {len(prefix)+1}    ·    Ctrl+Enter 执行")

    def modified(self, event=None):
        if self.internal_change:
            self.editor.edit_modified(False)
            return
        if self.editor.edit_modified():
            self.editor.tag_remove("error_line", "1.0", "end")
            self.editor.tag_remove("error_char", "1.0", "end")
            self.jump.configure(state="disabled")
            self.editor.edit_modified(False)
            self.remove_inline_error()
            self.color_sql()

    def set_sql(self, text):
        self.remove_inline_error()
        self.internal_change = True
        self.editor.delete("1.0", "end")
        self.editor.insert("1.0", text)
        self.editor.mark_set("pending_start", "1.0")
        self.editor.mark_gravity("pending_start", "left")
        self.editor.edit_reset()
        self.editor.edit_modified(False)
        self.internal_change = False
        self.location = None
        self.error_index = None
        self.jump.configure(state="disabled")
        self.color_sql()
        self.editor.focus_set()

    def demo(self):
        name = "student_" + uuid4().hex[:8]
        self.file_label.configure(text="  SQL  演示.sql   ")
        self.set_sql(f"CREATE TABLE {name}(id INT, name VARCHAR, age INT);\n"
                     f"INSERT INTO {name} VALUES(1, 'Alice', 20);\n"
                     f"INSERT INTO {name} VALUES(2, 'Bob', 17);\n"
                     f"INSERT INTO {name} VALUES(3, 'Carol', 22);\n"
                     f"SELECT id, name FROM {name} WHERE age >= 18;\n"
                     f"DELETE FROM {name} WHERE id = 1;\nSELECT * FROM {name};")

    def wrong(self):
        self.file_label.configure(text="  SQL  错误示例.sql   ")
        self.set_sql("-- 故意把 SELECT 写成 SELEC，执行后点击定位\nSELEC * FROM student;")

    def open_sql(self):
        path = filedialog.askopenfilename(filetypes=[("SQL 文件", "*.sql"), ("所有文件", "*.*")])
        if path:
            try:
                self.set_sql(Path(path).read_text(encoding="utf-8-sig"))
                self.file_label.configure(text=f"  SQL  {Path(path).name}   ")
            except (OSError, UnicodeError) as exc:
                messagebox.showerror("打开失败", str(exc))

    def save_sql(self):
        path = filedialog.asksaveasfilename(defaultextension=".sql", filetypes=[("SQL 文件", "*.sql")])
        if path:
            try:
                Path(path).write_text(self.editor.get("1.0", "end-1c"), encoding="utf-8")
                self.file_label.configure(text=f"  SQL  {Path(path).name}   ")
            except OSError as exc:
                messagebox.showerror("保存失败", str(exc))

    def choose_db(self):
        path = filedialog.asksaveasfilename(title="选择已有数据库或输入新文件名", confirmoverwrite=False,
                                           defaultextension=".db", filetypes=[("数据库", "*.db")])
        if path:
            self.path = Path(path)
            self.db_label.configure(text=self.path.name)
            self.message(f"已切换到 {self.path}。执行后将显示此数据库的结果。")
            self.stats.configure(text="缓存统计将在执行后显示")
            self.location = None
            self.jump.configure(state="disabled")

    def clear_database(self):
        """Delete the selected database file and recreate it on next execution."""
        if self.busy:
            messagebox.showinfo("正在执行", "请等待当前 SQL 执行完成后再清空数据库。")
            return
        if not messagebox.askyesno(
            "确认清空数据库",
            f"确定要删除本地数据库“{self.path.name}”中的全部表和数据吗？\n此操作不可撤销。",
            icon="warning",
        ):
            return
        try:
            if self.path.exists():
                self.path.unlink()
            self.set_sql("")
            self.db_label.configure(text=self.path.name)
            self.file_label.configure(text="  SQL  查询.sql   ")
            self.message(f"数据库已清空：{self.path.name}。下次执行 CREATE TABLE 时会创建新的空数据库。")
            self.stats.configure(text="数据库已清空")
            self.status.configure(text="数据库已清空")
        except OSError as exc:
            messagebox.showerror("清空失败", f"无法删除数据库文件：\n{exc}")

    def message(self, text, error=False):
        self.diagnostic.configure(state="normal", foreground="#f48771" if error else "#a5c99c", height=3 if error else 1)
        self.panel_title.configure(text="问题  ·  执行结果" if error else "执行结果")
        self.diagnostic.delete("1.0", "end")
        self.diagnostic.insert("1.0", text)
        self.diagnostic.configure(state="disabled")

    def remove_inline_error(self):
        widget = getattr(self, "error_widget", None)
        if widget is not None:
            self.internal_change = True
            try:
                if widget.winfo_exists():
                    self.editor.delete(self.editor.index(str(widget)))
                    widget.destroy()
                self.editor.edit_modified(False)
            finally:
                self.internal_change = False
            self.error_widget = None

    def run(self):
        if self.busy:
            return
        self.remove_inline_error()
        sql = self.editor.get("pending_start", "end-1c")
        if not sql.strip():
            self.message("请先输入 SQL。")
            return
        self.busy = True
        self.last_sql = sql
        self.editor.edit_modified(False)
        self.modified()
        self.editor.tag_remove("error_line", "1.0", "end")
        self.editor.tag_remove("error_char", "1.0", "end")
        self.editor.configure(state="disabled")
        self.jump.configure(state="disabled")
        for button in self.buttons:
            button.configure(state="disabled")
        self.status.configure(text="正在执行…")
        path = self.path
        def worker():
            try:
                self.queue.put(execute_sql(path, sql))
            except Exception as exc:
                self.queue.put(exc)
        Thread(target=worker, daemon=True).start()

    def poll(self):
        try:
            result = self.queue.get_nowait()
        except Empty:
            pass
        else:
            self.busy = False
            self.editor.configure(state="normal")
            for button in self.buttons:
                button.configure(state="normal")
            if isinstance(result, Exception):
                self.message(str(result), True)
                self.status.configure(text="执行失败")
            else:
                self.render(result)
        self.root.after(80, self.poll)

    def render(self, data):
        chunks, _ = split_complete_sql(self.last_sql)
        completed = len(data["results"])
        consumed = sum(len(chunk) for chunk in chunks[:completed])
        self.internal_change = True
        try:
            # Exclude Tk's mandatory terminal newline: deleting it can also
            # remove the preceding real newline belonging to locked history.
            self.editor.delete("pending_start", "end-1c")
            for chunk, result in zip(chunks, data["results"]):
                self.editor.insert("end-1c", chunk, ())
                lines = ["  " + result["message"]]
                if result["columns"]:
                    lines.append("  " + " | ".join(result["columns"]))
                    lines.extend("  " + " | ".join(map(str, row)) for row in result["rows"])
                self.editor.insert("end-1c", "\n" + "\n".join(lines) + "\n", ("output",))
            self.editor.mark_set("pending_start", "end-1c")
            self.editor.mark_gravity("pending_start", "left")
            self.editor.insert("end-1c", self.last_sql[consumed:], ())
            self.editor.edit_reset()
            self.editor.edit_modified(False)
        finally:
            self.internal_change = False
        self.color_sql()
        error = data["error"]
        self.location = error["location"] if error else None
        self.error_index = None
        if error:
            where = "未提供源码位置"
            if self.location:
                line, col = self.location["line"], self.location["column"]
                offset = sum(len(s)+1 for s in self.last_sql.split("\n")[:line-1]) + col-1
                history = self.editor.get("1.0", "pending_start")
                display_offset = len(history) + max(0, offset-consumed)
                full = self.editor.get("1.0", "end-1c")
                self.error_index = self.source_index(full, display_offset)
                where = f"第 {line} 行，第 {col} 列（本次提交）"
                if error["approximate"]:
                    where += "，语句起始位置"
            detail = f"{error['stage']}:{error['code']} · {where}\n{error['message']}"
            self.message(detail, True)
            # Embedded output is visible inline but never part of SQL returned by Text.get.
            label = tk.Label(self.editor, text=detail, background="#3b2525", foreground="#f48771",
                             justify="left", padx=10, pady=8)
            self.error_widget = label
            self.editor.window_create("end-1c", window=label)
            self.editor.edit_modified(False)
            if self.error_index:
                self.jump.configure(state="normal")
                self.locate()
        else:
            self.message(f"执行完成，共 {completed} 条语句，历史不会重复执行。")
            self.editor.mark_set("insert", "end-1c")
            self.editor.see("end")
        self.stats.configure(text="  |  ".join(f"{k} {v}" for k,v in data["stats"].items()))
        self.status.configure(text="执行遇到错误" if error else "执行完成")

    def text_command(self, *args):
        try:
            return ("ok", self.protected_text_command(*args))
        except tk.TclError as exc:
            return ("error", str(exc))

    def protected_text_command(self, *args):
        # Enforce the same boundary for typing, selection replacement, paste,
        # cut, undo and programmatic edits; navigation and copying stay available.
        if not self.internal_change and args:
            operation = args[0]
            if operation in ("insert", "delete", "replace"):
                starts = args[1::2] if operation == "delete" else args[1:2]
                if self.busy or any(self.editor.compare(index, "<", "pending_start") for index in starts):
                    return ""
                if operation in ("delete", "replace"):
                    # Clamp even single-index Delete at EOF. Otherwise Tk may
                    # consume the protected separator before pending_start.
                    limit = self.editor.index("end-1c")
                    def bounded(index):
                        index = self.editor.index(index)
                        return limit if self.editor.compare(index, ">", limit) else index
                    if operation == "delete":
                        normalized = [operation]
                        for i in range(1, len(args), 2):
                            start = bounded(args[i])
                            end = bounded(args[i+1] if i+1 < len(args) else f"{start}+1c")
                            normalized.extend((start, end))
                        args = tuple(normalized)
                    else:
                        args = (operation, bounded(args[1]), bounded(args[2]), *args[3:])
            if operation == "edit" and len(args) > 1 and args[1] in ("undo", "redo") and self.busy:
                return ""
        return self.editor.tk.call(self._original_text, *args)

    def locate(self):
        if not self.location:
            return
        start = getattr(self, "error_index", None)
        if not start:
            return
        self.editor.tag_add("error_line", f"{start} linestart", f"{start} lineend+1c")
        self.editor.tag_add("error_char", start, f"{start}+1c")
        self.editor.mark_set("insert", start)
        self.editor.see(start)
        self.editor.focus_set()
        self.draw_lines()

    def close(self):
        if self.busy:
            messagebox.showinfo("正在执行", "请等待执行结束后再关闭，确保数据保存完整。")
            return
        self.root.destroy()


def main():
    root = tk.Tk()
    Workbench(root, Path.cwd() / "desktop_demo.db")
    root.mainloop()
