"""Dependency-free local HTTP server backed by the real MiniDBMS engine."""

from __future__ import annotations

import argparse
import json
import logging
import os
from pathlib import Path
import shutil
import subprocess
from threading import RLock, Timer
from typing import Any
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
import webbrowser

from minidbms import __version__


LOGGER = logging.getLogger(__name__)
MAX_REQUEST_BYTES = 1_000_000
MAX_SQL_CHARS = 100_000
STATIC_ROOT = Path(__file__).with_name("static")
SQL_EXAMPLES_ROOT = Path(__file__).with_name("sql_examples")


def _sql_examples() -> list[dict[str, str]]:
    examples = []
    for path in sorted(SQL_EXAMPLES_ROOT.glob("*.sql")):
        sql = path.read_text(encoding="utf-8-sig")
        heading = next(
            (line.removeprefix("--").strip() for line in sql.splitlines() if line.strip().startswith("--")),
            path.stem,
        )
        prefix = path.name.split("_", 1)[0]
        level = "入门" if prefix <= "02" else "进阶" if prefix <= "06" else "综合"
        examples.append({"name": path.name, "title": heading, "level": level, "sql": sql})
    return examples


class MiniDBWebApplication:
    """Serialize browser requests through the local C++ core bridge."""

    def __init__(
        self,
        db_path: str | Path,
        buffer_capacity: int = 16,
        replacement_policy: str = "LRU",
    ) -> None:
        self.db_path = Path(db_path).resolve()
        self.buffer_capacity = buffer_capacity
        self.replacement_policy = replacement_policy.upper()
        self._lock = RLock()
        self._bridge = self._find_bridge()
        self.status()  # Fail during startup, not on the first browser request.

    @staticmethod
    def _find_bridge() -> Path:
        configured = os.environ.get("MINIDB_CORE_BRIDGE")
        if configured:
            path = Path(configured).expanduser().resolve()
            if path.is_file():
                return path
            raise RuntimeError(f"MINIDB_CORE_BRIDGE 指向的文件不存在：{path}")

        project_root = Path(__file__).resolve().parents[3]
        names = ("minidb_core_bridge.exe", "minidb_core_bridge")
        for directory in (project_root / "build-cpp", project_root / "build"):
            for name in names:
                candidate = directory / name
                if candidate.is_file():
                    return candidate
        discovered = shutil.which("minidb_core_bridge")
        if discovered:
            return Path(discovered).resolve()
        raise RuntimeError(
            "未找到 C++ 核心桥接程序；请先执行 cmake -S . -B build-cpp 和 "
            "cmake --build build-cpp"
        )

    def _invoke(self, operation: str, sql: str | None = None) -> dict[str, Any]:
        command = [
            str(self._bridge),
            operation,
            "--db", str(self.db_path),
            "--buffer-capacity", str(self.buffer_capacity),
            "--replacement-policy", self.replacement_policy,
        ]
        try:
            completed = subprocess.run(
                command,
                input=(sql or "").encode("utf-8"),
                capture_output=True,
                timeout=60,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"C++ 核心调用失败：{error}") from error
        try:
            payload = json.loads(completed.stdout.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            detail = completed.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(f"C++ 核心返回了无效响应：{detail or error}") from error
        if not isinstance(payload, dict):
            raise RuntimeError("C++ 核心返回值不是 JSON 对象")
        if completed.returncode != 0:
            error = payload.get("error") or {}
            raise RuntimeError(error.get("display") or error.get("message") or "C++ 核心执行失败")
        return payload

    def status(self) -> dict[str, Any]:
        with self._lock:
            payload = self._invoke("status")
            payload.pop("ok", None)
            return payload

    def execute(self, sql: str) -> dict[str, Any]:
        if not isinstance(sql, str):
            raise ValueError("sql must be a string")
        if not sql.strip():
            raise ValueError("请输入至少一条 SQL 语句")
        if len(sql) > MAX_SQL_CHARS:
            raise ValueError(f"SQL 内容不能超过 {MAX_SQL_CHARS} 个字符")

        with self._lock:
            return self._invoke("execute", sql)

    def reset(self) -> dict[str, Any]:
        """Replace the active demo database with a fresh, empty database."""
        with self._lock:
            return self._invoke("reset")

    def close(self) -> None:
        # Each bridge request owns and safely closes its C++ Database instance.
        return None


class MiniDBRequestHandler(BaseHTTPRequestHandler):
    application: MiniDBWebApplication

    _assets = {
        "/": ("index.html", "text/html; charset=utf-8"),
        "/index.html": ("index.html", "text/html; charset=utf-8"),
        "/assets/styles.css": ("styles.css", "text/css; charset=utf-8"),
        "/assets/app.js": ("app.js", "text/javascript; charset=utf-8"),
    }

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        path = urlparse(self.path).path
        if path == "/api/status":
            self._send_json(200, {"ok": True, **self.application.status()})
            return
        if path == "/api/examples":
            self._send_json(200, {"ok": True, "files": _sql_examples()})
            return
        if path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        asset = self._assets.get(path)
        if asset is None:
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        filename, content_type = asset
        try:
            content = (STATIC_ROOT / filename).read_bytes()
        except OSError:
            self._send_json(500, {"ok": False, "error": "frontend asset is missing"})
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self) -> None:  # noqa: N802 - stdlib handler API
        path = urlparse(self.path).path
        if path not in ("/api/execute", "/api/database/reset"):
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_REQUEST_BYTES:
                raise ValueError("请求内容为空或过大")
            document = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(document, dict):
                raise ValueError("请求必须是 JSON 对象")
            if path == "/api/database/reset":
                if self.headers.get_content_type() != "application/json":
                    raise ValueError("清空数据库接口只接受 application/json")
                if document.get("confirmation") != "RESET_DATABASE":
                    raise ValueError("缺少清空数据库确认标记")
                payload = self.application.reset()
            else:
                payload = self.application.execute(document.get("sql"))
        except (ValueError, UnicodeError, json.JSONDecodeError) as error:
            self._send_json(400, {"ok": False, "error": str(error)})
            return
        except RuntimeError as error:
            LOGGER.error("database request failed: %s", error)
            self._send_json(500, {"ok": False, "error": str(error)})
            return
        except Exception:
            LOGGER.exception("unhandled web request error")
            self._send_json(500, {"ok": False, "error": "服务器执行失败，请查看终端日志"})
            return
        self._send_json(200, payload)

    def _send_json(self, status: int, value: Any) -> None:
        content = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(content)

    def log_message(self, format: str, *args: Any) -> None:
        LOGGER.info("%s - %s", self.address_string(), format % args)


def create_server(
    application: MiniDBWebApplication,
    host: str = "127.0.0.1",
    port: int = 8765,
) -> ThreadingHTTPServer:
    class BoundHandler(MiniDBRequestHandler):
        pass

    BoundHandler.application = application
    server = ThreadingHTTPServer((host, port), BoundHandler)
    server.daemon_threads = True
    return server


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MiniDBMS local browser interface")
    parser.add_argument("--version", action="version", version=f"MiniDBMS Web {__version__}")
    parser.add_argument("--db", type=Path, default=Path("minidb-web.db"), help="database file")
    parser.add_argument("--host", default="127.0.0.1", help="listen address")
    parser.add_argument("--port", type=int, default=8765, help="listen port")
    parser.add_argument("--buffer-capacity", type=int, default=8, help="cached page count")
    parser.add_argument("--replacement-policy", default="LRU", help="LRU or FIFO")
    parser.add_argument("--no-browser", action="store_true", help="do not open the browser automatically")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        application = MiniDBWebApplication(
            args.db, args.buffer_capacity, args.replacement_policy
        )
        server = create_server(application, args.host, args.port)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"MiniDBMS Web 启动失败：{error}")
        return 1

    host_for_browser = "127.0.0.1" if args.host in ("0.0.0.0", "::") else args.host
    url = f"http://{host_for_browser}:{server.server_port}"
    print(f"MiniDBMS Web 已启动：{url}")
    print(f"数据库文件：{application.db_path}")
    print("按 Ctrl+C 停止服务。")
    if not args.no_browser:
        Timer(0.35, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n正在关闭 MiniDBMS Web...")
    finally:
        server.server_close()
        application.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
