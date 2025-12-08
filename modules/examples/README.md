# QuickJS + libuv Examples

The `qjsuv` sample embeds QuickJS and exposes a small `uv` module backed by libuv. The module provides:

- `setTimeout(fn, delayMs)` / `setInterval(fn, delayMs)` returning numeric timer ids
- `clearTimer(id)` to cancel either timer type
- `sleep(delayMs)` returning a promise resolved after the requested delay

## Building

From the project root run the regular CMake configure + build workflow. The `qjsuv` target links against `libuv::uv` and `qjs`, and reuses the shared implementation in `modules/src/js_uv_module.c`.

Example (using CMake presets):

```powershell
cmake --preset default
cmake --build --preset default --target qjsuv
```

Running the demo prints the timer activity and promise resolution order, demonstrating that libuv timers and the QuickJS job queue work together:

```powershell
./build/modules/examples/qjsuv
```
