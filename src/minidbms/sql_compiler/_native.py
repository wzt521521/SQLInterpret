"""Private length-prefixed UTF-8 transport; no shell or SQL interpolation."""
import json
import os
from pathlib import Path
import subprocess

from minidbms.common.errors import ErrorStage
from .errors import CompilerError, native_error


class Writer:
    def __init__(self):
        self.data = bytearray()

    def number(self, value):
        self.data.extend(f"{value}\n".encode("ascii"))

    def string(self, value):
        encoded = value.encode("utf-8")
        self.number(len(encoded))
        self.data.extend(encoded)

    def strings(self, values):
        self.number(len(values))
        for value in values:
            self.string(value)


def request(operation, writer):
    suffix = ".exe" if os.name == "nt" else ""
    default = Path(__file__).resolve().parent / "native" / "build" / ("minisql_bridge" + suffix)
    executable = Path(os.environ.get("MINISQL_BRIDGE", default))
    try:
        result = subprocess.run([str(executable), operation], input=bytes(writer.data),
                                capture_output=True, timeout=30,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    except FileNotFoundError as error:
        raise CompilerError(ErrorStage.EXECUTION, "NATIVE_COMPILER_NOT_FOUND",
                            "Run python -m minidbms.sql_compiler.build_native, or set MINISQL_BRIDGE.") from error
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CompilerError(ErrorStage.EXECUTION, "NATIVE_COMPILER_FAILED", str(error)) from error
    try:
        payload = json.loads(result.stdout)
        if result.returncode not in (0, 1) or not isinstance(payload, dict):
            raise ValueError("Native compiler terminated unexpectedly")
        diagnostic = payload["error"]
        if diagnostic is not None:
            raise native_error(diagnostic)
        if result.returncode != 0:
            raise ValueError("Native compiler returned failure without a diagnostic")
        return payload["result"]
    except (ValueError, KeyError, TypeError) as error:
        raise CompilerError(ErrorStage.EXECUTION, "NATIVE_PROTOCOL_ERROR", str(error)) from error


def sql_request(operation, sql, schemas=(), optimize=True):
    writer = Writer()
    try:
        writer.string(sql)
    except UnicodeEncodeError as error:
        raise CompilerError(ErrorStage.LEXICAL, "INVALID_UTF8", "SQL contains invalid Unicode.") from error
    if operation == "compile":
        writer.number(len(schemas))
        for schema in schemas:
            writer.string(schema.table_name)
            writer.number(len(schema.columns))
            for column in schema.columns:
                writer.string(column.name)
                writer.string(column.data_type.value)
        writer.number(int(optimize))
    return request(operation, writer)
