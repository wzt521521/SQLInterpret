"""Native diagnostics conform to the team's DBError contract."""
from minidbms.common.errors import DBError, ErrorStage
from minidbms.common.types import SourceLocation


class CompilerError(DBError):
    def __init__(self, stage, code, message, location=None, actual="", expected=()):
        super().__init__(stage, code, message, location)
        self.actual = actual
        self.expected = tuple(expected)


def native_error(error):
    stage = error["stage"]
    # INTERNAL is private to C++; no change to the shared ErrorStage enum.
    stage = ErrorStage.EXECUTION if stage == "INTERNAL" else ErrorStage(stage)
    return CompilerError(stage, error["code"], error["message"],
                         SourceLocation(**error["location"]),
                         error.get("actual", ""), error.get("expected", ()))
