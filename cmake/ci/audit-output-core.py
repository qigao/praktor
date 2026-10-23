#!/usr/bin/env python3
"""The output scanner archive must remain C-only and allocation-free."""
import subprocess
import sys

symbols = subprocess.check_output(["nm", "-C", sys.argv[1]], text=True)
for forbidden in ("std::", "__cxa_", "__gxx_", "actions::", "json_", "regex"):
    if forbidden in symbols:
        raise SystemExit("Output core has a runtime dependency: " + forbidden)
undefined = subprocess.check_output(["nm", "-u", sys.argv[1]], text=True)
for line in undefined.splitlines():
    if line.split() and line.split()[-1].lstrip("_") in {
        "malloc", "calloc", "realloc", "free", "aligned_alloc", "alloca"
    }:
        raise SystemExit("Output core allocates: " + line)
print("Output core: C-only scanner and JSON encoder, no allocator or DOM")
