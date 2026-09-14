"""Interactive and file-driven CLI for the persistent MiniDBMS."""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
import sys

from minidbms import __version__
from minidbms.common.errors import DBError
from minidbms.common.types import ExecutionResult
from minidbms.engine.database import Database


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MiniDBMS SQL command-line interface")
    parser.add_argument("--version", action="version", version=f"MiniDBMS {__version__}")
    parser.add_argument("--db", type=Path, default=Path("minidb.db"), help="database file")
    parser.add_argument("--buffer-capacity", type=int, default=16, help="cached page count")
    parser.add_argument("--replacement-policy", default="LRU", help="LRU or FIFO")
    parser.add_argument("--file", type=Path, help="execute SQL from a UTF-8 file")
    parser.add_argument("--stats", action="store_true", help="print final buffer statistics")
    parser.add_argument("--cache-log", action="store_true", help="show cache hit and eviction logs")
    return parser


def split_complete_sql(text: str) -> tuple[list[str], str]:
    """Frame statements without treating semicolons in strings/comments as delimiters."""
    complete: list[str] = []
    start = index = 0
    state = "normal"
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if current == "'":
                state = "string"
            elif current == "-" and following == "-":
                state = "line_comment"
                index += 1
            elif current == "/" and following == "*":
                state = "block_comment"
                index += 1
            elif current == ";":
                complete.append(text[start:index + 1])
                start = index + 1
        elif state == "string":
            if current == "'" and following == "'":
                index += 1
            elif current == "'":
                state = "normal"
        elif state == "line_comment":
            if current in "\r\n":
                state = "normal"
        elif state == "block_comment" and current == "*" and following == "/":
            state = "normal"
            index += 1
        index += 1
    return complete, text[start:]


def format_result(result: ExecutionResult) -> str:
    if result.columns:
        lines = [" | ".join(result.columns)]
        lines.extend(" | ".join(str(value) for value in row) for row in result.rows)
        lines.append(result.message)
        return "\n".join(lines)
    return result.message or f"{result.affected_rows} row(s) affected"


def run_sql(db: Database, sql: str) -> bool:
    try:
        for result in db.execute(sql):
            print(format_result(result))
        return True
    except DBError as error:
        print(error, file=sys.stderr)
        return False


def run_text(db: Database, text: str) -> bool:
    statements, remaining = split_complete_sql(text)
    succeeded = True
    for statement in statements:
        succeeded = run_sql(db, statement) and succeeded
    if remaining.strip():
        succeeded = run_sql(db, remaining) and succeeded
    return succeeded


def interactive(db: Database) -> None:
    pending = ""
    while True:
        try:
            line = input("MiniDB > " if not pending.strip() else "    ... > ")
        except EOFError:
            print()
            if pending.strip():
                run_sql(db, pending)
            return
        command = line.strip().lower()
        if not pending.strip() and command in ("quit", "exit"):
            return
        if not pending.strip() and command == "stats":
            print(db.stats())
            continue
        pending += line + "\n"
        statements, pending = split_complete_sql(pending)
        for statement in statements:
            run_sql(db, statement)
        if not pending.strip():
            pending = ""


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.cache_log:
        logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stderr)
    try:
        with Database(args.db, args.buffer_capacity, args.replacement_policy) as db:
            if args.file is None:
                interactive(db)
                succeeded = True
            else:
                succeeded = run_text(db, args.file.read_text(encoding="utf-8-sig"))
            if args.stats:
                print(db.stats())
            return 0 if succeeded else 1
    except (DBError, OSError, UnicodeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
