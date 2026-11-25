#include "PictureMessage.h"

LRESULT CALLBACK ControllerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        auto check = CreateWindowW(L"BUTTON", L"Перевернуть по горизонтали",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 20, 20, 300, 26,
            window, reinterpret_cast<HMENU>(flipCheckId), instance, nullptr);
        auto button = CreateWindowW(L"BUTTON", L"Нарисовать", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 58, 130, 30, window, reinterpret_cast<HMENU>(drawButtonId), instance, nullptr);
        const auto font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(check, WM_SETFONT, font, TRUE);
        SendMessageW(button, WM_SETFONT, font, TRUE);
        return check && button ? 0 : -1;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == drawButtonId && HIWORD(wParam) == BN_CLICKED) {
            const HWND viewer = FindWindowW(viewerClass, nullptr);
            if (!viewer) {
                MessageBoxW(window, L"Сначала запустите PictureViewer.exe.", L"Окно просмотра",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            const bool flip = IsDlgButtonChecked(window, flipCheckId) == BST_CHECKED;
            const UINT draw = PictureMessage();
            if (!draw || !PostMessageW(viewer, draw, flip, 0)) {
                MessageBoxW(window, L"Не удалось передать команду.", L"Ошибка", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    WNDCLASSW type{};
    type.lpfnWndProc = ControllerProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = controllerClass;
    if (!RegisterClassW(&type)) return 1;
    return WindowLoop(CreateWindowW(controllerClass, L"Практическая №1 — управление",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 80, 100, 350, 145,
        nullptr, nullptr, instance, nullptr), show);
}
