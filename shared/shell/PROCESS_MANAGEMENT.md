# Process ownership

All Shell and Program calls use the C11 `Salts::Process` implementation.
The former Praktor POSIX fork/pipe/waitpid implementation and Windows
CreateProcess/Job Object implementation have been deleted. There is no alternate
native Shell/Program backend in Praktor.

`ShellExecutor`, `ProcessExecutor`, `ManagedProcess` and `ShellResult` remain C++
call-site adapters for existing workflow, script, service and actions callers.
They translate configuration and terminal results; they own no OS handles.
Shell command parsing and process-tree termination belong to Salts.

`shared/process/src/process_driver.c` is the one Praktor Process driver used by
both this facade and the C11 Session. The old `process_adapter.cpp` and duplicated
C++ stream-buffer helpers have been deleted. The C++ facade only converts ports,
maps results, synchronizes observers and schedules one joined thread. Only this
thread calls the C driver. Cancellation and observers
access synchronized adapter metadata, never a native Process handle. There are
three fixed 4096-byte I/O buffers and eight bounded request slots. Input and both
output streams progress concurrently through Process callbacks. Darwin stdin
peer-close protection belongs to the Process-owned parent endpoint, supplied by
SaltsUtils `4.3.0-rc.11` or the pinned P1 candidate. Praktor changes neither the
caller nor the child signal policy. Captured stdout
and stderr share `max_output_bytes` (16 MiB by default), with a bounded terminal
error diagnostic. Stream callbacks preserve per-stream lines and trailing text.

`start()` returns an owned handle; `execute()` is `start().wait()`. The obsolete
unowned `executeAsync()`, raw-PID `killProcess()`, early `pid()` observer and custom
`cancel_grace_ms` policy were removed. `ShellResult::pid` remains terminal
observation. Cancellation policy belongs to Salts, with no Praktor signal timer.

Destruction requests cancellation and joins the lifecycle driver. The driver
retains every callback and I/O buffer until close, terminal acknowledgements,
quiescence and successful destruction. Transient cleanup errors are retried by
the same owner and reported as failure. This facade is synchronous on teardown:
a persistent cleanup fault can keep wait/destruction blocked; it never force-frees
live leases or detaches their storage.

The P1 implementation is now merged as `qigao/salts-utils@f675382f739c5691bb38029eb6e5737b8cf08e24`
(PR #689), retaining the hardened close/acknowledgement, stats and retainer
paths while adding the Darwin endpoint fix.

The cooperative C11 Session uses the same C driver with its explicit fixed-capacity
Process retainer for bounded synchronous teardown and later owner-driven drain.
The driver reserves before native admission and detaches callback/user storage
before close. Late completions retain only C buffers, never a shared C++ Session
or AST. P1 retainer/shutdown APIs are an explicit build capability enabled by the
experimental Session; the released SDK baseline uses the same Process owner with
synchronous teardown and does not require unpublished P1 headers or symbols.

Capture and input snapshots are allocated before Process start; subsequent
drive/close/handoff operations allocate no driver storage. The capture bound
applies across both streams. Each stream's capture and optional line scratch
has that fixed bound; three 4096-byte I/O buffers remain live until native destroy.

Tests use `Salts::TinyTest` and cover argv, inherited environment and overrides,
working directories, duplex 256 KiB input/output, capture limits, stream callbacks,
nonblocking owned start, cancellation, timeout, destructor tree cleanup and spawn
failures. Core-only ASan/UBSan, consumer TSan and complete-source unsuppressed TSan
also run this facade's tests.
