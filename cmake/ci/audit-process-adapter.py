#!/usr/bin/env python3
"""Require Salts::Process linkage and reject a second Praktor OS process backend."""
import re
import subprocess
import sys

facade = subprocess.check_output(['nm', '-u', sys.argv[1]], text=True)
symbols = subprocess.check_output(['nm', '-u', sys.argv[2]], text=True)
for symbol in ('praktor_process_worker_start', 'praktor_process_worker_wait',
               'praktor_process_worker_cancel', 'praktor_process_worker_destroy'):
    if not re.search(r'\b' + symbol + r'\b', facade):
        raise SystemExit('Compatibility facade bypasses the C11 driver: ' + symbol)
if re.search(r'\b(?:cflow_process_|praktor_process_driver_)\w+', facade):
    raise SystemExit('C++ facade still drives Salts::Process directly')
if re.search(r'(?:_ZNSt6thread|_ZNSt5mutex|_ZNSt18condition_variable)', facade):
    raise SystemExit('C++ facade still owns process scheduling/synchronization')
if re.search(r'__cxa_|__gxx_|_Z[NST]|\bstd::', symbols):
    raise SystemExit('C11 Process driver requires a C++ runtime')
for symbol in ('cmeta_thread_create', 'cmeta_thread_join', 'cmeta_mutex_lock', 'cmeta_cond_timedwait',
               'praktor_process_driver_start', 'praktor_process_driver_run',
               'cflow_process_start', 'cflow_process_try_read_stdout',
               'cflow_process_try_read_stderr', 'cflow_process_try_write_stdin',
               'cflow_process_close', 'cflow_process_run_ready',
               'cflow_process_is_quiescent', 'cflow_process_destroy'):
    if not re.search(r'\b' + symbol + r'\b', symbols):
        raise SystemExit('Process adapter is missing C11 ownership call: ' + symbol)
if re.search(r'\bcmeta_thread_destroy\b', symbols):
    raise SystemExit('C11 Process worker must join, never detach its driver thread')
for symbol in ('fork', 'vfork', 'posix_spawn', 'execve', 'waitpid', 'pipe', 'pipe2',
               'poll', 'kill', 'CreateProcessA', 'CreateProcessW', 'CreatePipe',
               'CreateJobObjectW', 'TerminateProcess', 'TerminateJobObject'):
    if re.search(r'\b' + symbol + r'\b', symbols + facade):
        raise SystemExit('Praktor OS process backend remains: ' + symbol)
print('Process facade: Salts::Process owns native spawn, I/O and teardown')
