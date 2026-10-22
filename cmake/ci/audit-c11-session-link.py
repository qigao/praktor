#!/usr/bin/env python3
"""Fail if an independently linked Session brings in legacy execution symbols."""
import subprocess
import sys

symbols = subprocess.check_output(["nm", "-C", sys.argv[1]], text=True)
for forbidden in ("actions::Executor", "actions::Blackboard", "actions::ExecutionContext", "Praktor::Shell::"):
    if forbidden in symbols:
        raise SystemExit("Independent C11 Session linked legacy symbols: " + forbidden)
print("Independent C11 Session: no legacy Executor, Blackboard, ExecutionContext or Shell symbols")
