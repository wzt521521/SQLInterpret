"""Shared desktop execution service with source diagnostics."""
from pathlib import Path
from dataclasses import asdict
from minidbms.common.errors import DBError
from minidbms.engine import Database

def execute_sql(path: Path, sql: str) -> dict:
    # Normalize editor newlines while retaining Unicode character coordinates.
    sql = sql.replace("\r\n", "\n").replace("\r", "\n")
    results = []
    error = None
    current = None
    stats = {}
    try:
        with Database(path) as db:
            try:
                statements = db.compiler.prepare(sql)
                for current in statements:
                    detail = db.compiler.compile_statement(current, db.catalog)
                    results.append(asdict(db.executor.execute(detail.plan_after)))
            except DBError as exc:
                location = exc.location or (current.location if current else None)
                error = {
                    "stage": exc.stage.value, "code": exc.code, "message": exc.message,
                    "location": asdict(location) if location else None,
                    "approximate": exc.location is None and location is not None,
                }
            # Persist successful statements even if a subsequent statement failed.
            db.storage.flush_all()
            stats = db.stats()
    except (DBError, OSError) as exc:
        error = {"stage": "STORAGE", "code": getattr(exc, "code", "IO_ERROR"),
                 "message": str(exc), "location": None, "approximate": False}
    return {"results": results, "error": error, "stats": stats, "database": path.name}


