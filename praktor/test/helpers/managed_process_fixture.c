#include <stddef.h>
#ifdef _WIN32
#include <windows.h>

static const wchar_t kWindowClassName[] = L"PraktorManagedProcessFixtureWindow";

static LRESULT CALLBACK fixtureWindowProc(HWND window,
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

int main(void) {
  const HINSTANCE instance = GetModuleHandleW(NULL);
  WNDCLASSW window_class = {0};
  window_class.lpfnWndProc = fixtureWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kWindowClassName;
  if (RegisterClassW(&window_class) == 0) {
    return 2;
  }

  const HWND window = CreateWindowExW(
      0, kWindowClassName, L"Praktor managed process fixture",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 200, NULL,
      NULL, instance, NULL);
  if (window == NULL) {
    return 3;
  }

  MSG message = {0};
  while (GetMessageW(&message, NULL, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return (int)message.wParam;
}
#else
int main(void) { return 0; }
#endif
