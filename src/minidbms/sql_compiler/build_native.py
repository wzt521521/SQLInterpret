"""Build the bundled C++17 compiler: python -m minidbms.sql_compiler.build_native."""
from pathlib import Path
import os
import shutil
import subprocess

NATIVE = Path(__file__).resolve().parent / "native"
BUILD = NATIVE / "build"


def build():
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if compiler is None:
        raise RuntimeError("C++17 compiler missing; install g++ or set CXX to clang++")
    BUILD.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2",
             "-I", str(NATIVE / "include")]
    objects = []
    for source in sorted((NATIVE / "src").glob("*.cc")):
        obj = BUILD / (source.stem + ".o")
        subprocess.run([compiler, *flags, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    for name, source in [("minisql_cli", "main.cc"), ("minisql_bridge", "bridge.cc")]:
        libraries = ["-static"] if os.name == "nt" else []
        if os.name == "nt" and name == "minisql_cli":
            libraries.append("-lshell32")
        exe = BUILD / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([compiler, *flags, str(NATIVE / "app" / source), *objects,
                        *libraries, "-o", str(exe)], check=True)
    print(f"Built C++ compiler and Python bridge in {BUILD}")


if __name__ == "__main__":
    build()
