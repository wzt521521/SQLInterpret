"""Desktop execution integration with the real compiler and database."""

from minidbms.desktop.service import execute_sql


def test_empty_selection_and_undo_do_not_escape_mainloop(tmp_path):
    import tkinter as tk
    import pytest
    from minidbms.desktop.app import Workbench

    try:
        root = tk.Tk()
    except tk.TclError:
        pytest.skip("Tk display unavailable")
    root.withdraw()
    app = Workbench(root, tmp_path / "selection.db")
    failures = []
    root.report_callback_exception = lambda *error: failures.append(error)
    try:
        app.set_sql("")
        root.update()

        def native_edits():
            # Tk's typing and deletion bindings routinely try sel.first/sel.last
            # under Tcl catch. Exercise the actual Tcl/Python callback boundary.
            for command in (
                f"{app.editor._w} delete sel.first sel.last",
                f"{app.editor._w} replace sel.first sel.last x",
                f"{app.editor._w} edit undo",
                f"{app.editor._w} edit redo",
            ):
                assert root.tk.call("catch", command) == 1
            root.tk.call("::tk::TextInsert", app.editor._w, "SELECT")
            app.editor.event_generate("<<Cut>>")
            app.editor.event_generate("<<Clear>>")
            assert app.editor.get("1.0", "end-1c") == "SELECT"
            root.after_idle(root.quit)

        root.after(0, native_edits)
        root.after(3000, root.quit)
        root.mainloop()
        assert not failures
        root.update()
        assert "keyword" in app.editor.tag_names("1.0")
    finally:
        root.destroy()


def test_native_window_workflow(tmp_path):
    import time
    import tkinter as tk
    import pytest
    from minidbms.desktop.app import Workbench

    try:
        root = tk.Tk()
    except tk.TclError:
        pytest.skip("Tk display unavailable")
    root.withdraw()
    app = Workbench(root, tmp_path / "gui.db")
    app.demo()

    def run_and_wait():
        root.update()
        app.run()
        deadline = time.monotonic() + 15
        while app.busy and time.monotonic() < deadline:
            root.update()
            time.sleep(0.01)
        assert not app.busy
        root.update()

    try:
        run_and_wait()
        assert app.editor.get("pending_start", "end-1c") == ""
        history = app.editor.get("1.0", "pending_start")
        assert "Carol" in history
        assert history.endswith("\n")
        assert app.editor.index("pending_start").endswith(".0")
        for start, end in (("end-1c", "end"), ("pending_start", "end")):
            app.editor.delete(start, end)
            assert app.editor.get("1.0", "pending_start") == history
        app.editor.delete("end-1c")
        assert app.editor.get("1.0", "pending_start") == history
        run_and_wait()
        assert app.editor.get("1.0", "end-1c") == history
        app.editor.insert("end-1c", "CREATE TABLE extra(id INT); INSERT INTO extra VALUES(9); SELECT * FROM extra;")
        root.update()
        assert "keyword" in app.editor.tag_names("pending_start")
        assert "output" not in app.editor.tag_names("pending_start")
        run_and_wait()
        assert app.location is None
        assert app.editor.get("1.0", "end-1c").startswith(history + "CREATE TABLE extra")
        assert execute_sql(app.path, "SELECT * FROM extra;")["results"][0]["rows"] == [[9]]
        history = app.editor.get("1.0", "pending_start")
        app.editor.delete("1.0", "end")
        app.editor.insert("1.0", "tamper")
        assert app.editor.get("1.0", "pending_start") == history
        app.editor.tag_add("sel", "1.0", "end-1c")
        app.editor.event_generate("<<Cut>>")
        root.update()
        assert app.editor.get("1.0", "pending_start") == history
        app.editor.tag_remove("sel", "1.0", "end")
        app.editor.insert("pending_start", "temporary")
        app.editor.edit_undo()
        root.update()
        assert app.editor.get("1.0", "end-1c") == history
        app.editor.insert("pending_start", "SELECT * FROM extra;")
        root.update()
        run_and_wait()
        assert app.location is None
        assert app.editor.get("1.0", "end-1c").startswith(history + "SELECT * FROM extra;")
        assert app.editor.index("pending_start").endswith(".0")
        app.set_sql("INSERT INTO extra VALUES(10);\nSELECT missing FROM extra;\nSELECT * FROM extra;")
        run_and_wait()
        assert app.location is not None
        assert app.editor.get(*app.editor.tag_ranges("error_char")) == "m"
        pending = app.editor.get("pending_start", "end-1c")
        assert pending == "\nSELECT missing FROM extra;\nSELECT * FROM extra;"
        app.editor.delete("pending_start", "end")
        app.editor.insert("pending_start", "SELECT * FROM extra;")
        root.update()
        run_and_wait()
        assert execute_sql(app.path, "SELECT * FROM extra;")["results"][0]["rows"] == [[9], [10]]
        assert app.location is None
        app.set_sql("CREATE TABLE strings(value VARCHAR); INSERT INTO strings VALUES('中文😀;ok'); SELECT * FROM strings;")
        run_and_wait()
        assert app.location is None
        assert execute_sql(app.path, "SELECT * FROM strings;")["results"][0]["rows"] == [["中文😀;ok"]]
        assert app.editor.get("pending_start", "end-1c") == ""
        app.wrong()
        run_and_wait()
        assert app.location == {"line": 2, "column": 1}
        assert app.editor.get(*app.editor.tag_ranges("error_char")) == "S"
        app.set_sql("SELECT '中文😀' @ FROM t;")
        run_and_wait()
        assert app.editor.get(*app.editor.tag_ranges("error_char")) == "@"
        app.set_sql("-- corrected")
        root.update()
        assert not app.editor.tag_ranges("error_char")
        assert str(app.jump["state"]) == "disabled"
    finally:
        root.destroy()


def test_success_and_persistence(tmp_path):
    path = tmp_path / "web.db"
    result = execute_sql(path, "CREATE TABLE t(id INT); INSERT INTO t VALUES(7); SELECT * FROM t;")
    assert result["error"] is None
    assert result["results"][-1]["rows"] == [[7]]
    assert execute_sql(path, "SELECT * FROM t;")["results"][0]["rows"] == [[7]]


def test_multiline_semantic_error_preserves_completed_results(tmp_path):
    path = tmp_path / "web.db"
    result = execute_sql(path, "CREATE TABLE t(id INT);\nINSERT INTO t VALUES(7);\nSELECT missing FROM t;")
    assert len(result["results"]) == 2
    assert result["error"]["stage"] == "SEMANTIC"
    assert result["error"]["location"] == {"line": 3, "column": 8}
    assert execute_sql(path, "SELECT * FROM t;")["results"][0]["rows"] == [[7]]


def test_syntax_error_and_unicode_coordinates(tmp_path):
    path = tmp_path / "web.db"
    result = execute_sql(path, "-- 中文注释\r\nSELEC * FROM t;")
    assert result["error"]["stage"] == "SYNTAX"
    assert result["error"]["location"] == {"line": 2, "column": 1}
    sql = "SELECT '中文😀' @ FROM t;"
    result = execute_sql(path, sql)
    assert result["error"]["stage"] == "LEXICAL"
    assert result["error"]["location"]["column"] == sql.index("@") + 1
