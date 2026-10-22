#!/usr/bin/env python3
"""Reject C++ runtime/legacy execution in the C-only Process executable."""
import subprocess
import sys

binary = sys.argv[1]
symbols = subprocess.check_output(["nm", "-C", binary], text=True)
for symbol in ("std::", "__cxa_throw", "__gxx_personality", "actions::", "Praktor::Shell::"):
    if symbol in symbols:
        raise SystemExit("C-only Process linked a C++ execution/runtime symbol: " + symbol)
needed = subprocess.check_output(["readelf", "-d", binary], text=True)
for library in ("libstdc++", "libc++", "libc++abi"):
    if library in needed:
        raise SystemExit("C-only Process links a C++ runtime: " + library)
print("C-only Process: no C++ runtime, Executor or Shell facade")
