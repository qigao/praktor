# Native process ownership

`shared/process` owns Praktor's C process driver and bounded recovery slots.
SaltsBase `process_async` owns the OS process, native pipe endpoints, pending
requests and terminal result. Praktor never detaches its opaque raw process.

The driver allocates input, capture and line buffers before native admission.
It retains one address-stable async handle, with at most one pending request per
stream. Accepted I/O borrows the buffers through terminal callbacks. Capture is
bounded jointly across stdout and stderr.

An optional `praktor_process_retainer` reserves a fixed-capacity slot before
spawn. Full capacity returns ENOBUFS before any process side effect. One
exclusive driver operates each retainer. Workers use distinct retainers, and
the application Owner takes over after callback settlement.

Close withdraws admission and detaches application emit callbacks before
cancellation/drain. If native ownership remains live, close returns EBUSY.
Release then either refuses ownership loss or adopts the complete C payload
into its reserved slot. The async handle's address stays fixed during transfer.
Recovery destroys a process only after native quiescence, then releases its
buffers. Errors retain ownership and propagate to the cohort publication gate.
No close, release or recovery turn allocates storage.

Reservations use owner identity and increasing serials. Foreign, stale and
copied-owner reservations cannot release a current slot. Unadopted reservations
still count as pending and block retainer destruction and output publication.

The public contracts are in `praktor/process_driver.h` and
`praktor/process_retainer.h`. Formal driver, worker, cohort, Session, C API and
CLI tests run through the complete root CTest user preset.
