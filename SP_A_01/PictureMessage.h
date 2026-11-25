#pragma once
#define NOMINMAX
#include <windows.h>

inline constexpr wchar_t viewerClass[] = L"Practice01.PictureViewer";
inline constexpr wchar_t controllerClass[] = L"Practice01.PictureController";
inline constexpr INT_PTR flipCheckId = 101;
inline constexpr INT_PTR drawButtonId = 102;

inline UINT PictureMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Practice01.DrawPicture");
    return message;
}

inline int WindowLoop(HWND window, int show)
{
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
