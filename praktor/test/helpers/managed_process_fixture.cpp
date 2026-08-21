#ifdef _WIN32
#include <windows.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"PraktorManagedProcessFixtureWindow";

LRESULT CALLBACK fixtureWindowProc(HWND window,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace

int main() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = fixtureWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kWindowClassName;
  if (RegisterClassW(&window_class) == 0) {
    return 2;
  }

  const HWND window = CreateWindowExW(
      0, kWindowClassName, L"Praktor managed process fixture",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 200, nullptr,
      nullptr, instance, nullptr);
  if (window == nullptr) {
    return 3;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}
#else
int main() { return 0; }
#endif
