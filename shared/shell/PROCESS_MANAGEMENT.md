# Process ownership

All Shell and Program calls use the C11 `Salts::Process` implementation.
The former Praktor POSIX fork/pipe/waitpid implementation and Windows
CreateProcess/Job Object implementation have been deleted. There is no alternate
native Shell/Program backend in Praktor.

`ShellExecutor`, `ProcessExecutor`, `ManagedProcess` and `ShellResult` remain C++
call-site adapters for existing workflow, script, service and actions callers.
They translate configuration and terminal results; they own no OS handles.
Shell command parsing and process-tree termination belong to Salts.

The synchronous facade has one joined driver thread per invocation. Only this
thread calls Process start/submit/run/close/destroy. Cancellation and observers
access synchronized adapter metadata, never a native Process handle. There are
three fixed 4096-byte I/O buffers and eight bounded request slots. Input and both
output streams progress concurrently through Process callbacks. On POSIX, the
dedicated driver blocks SIGPIPE only after spawn and keeps it blocked until
thread exit, so early stdin peer-close becomes an I/O result without changing
the caller or child signal policy. Captured stdout
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

The cooperative C11 Session continues to use its separate explicit fixed-capacity
Process retainer for bounded synchronous teardown and later owner-driven drain.
This change does not replace or bypass that retainer contract.

Tests use `Salts::TinyTest` and cover argv, inherited environment and overrides,
working directories, duplex 256 KiB input/output, capture limits, stream callbacks,
nonblocking owned start, cancellation, timeout, destructor tree cleanup and spawn
failures. Core-only ASan/UBSan, consumer TSan and complete-source unsuppressed TSan
also run this facade's tests.
