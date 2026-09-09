"""Temporary CLI entry point for the initial project scaffold."""

from __future__ import annotations

import argparse

from minidbms import __version__


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MiniDBMS command-line interface")
    parser.add_argument("--version", action="version", version=f"MiniDBMS {__version__}")
    return parser


def main(argv: list[str] | None = None) -> int:
    build_parser().parse_args(argv)
    print("MiniDBMS project scaffold is ready; feature implementation is pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
