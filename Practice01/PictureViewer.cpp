#include "PictureMessage.h"
#include <algorithm>

namespace {
constexpr int pictureWidth = 260, pictureHeight = 160;
bool visible = false;
bool flipped = false;

void DrawPicture(HDC canvas)
{
    RECT sky{0, 0, pictureWidth, pictureHeight};
    HBRUSH blue = CreateSolidBrush(RGB(193, 226, 248));
    FillRect(canvas, &sky, blue);
    DeleteObject(blue);
    RECT ground{0, 115, pictureWidth, pictureHeight};
    HBRUSH green = CreateSolidBrush(RGB(101, 157, 97));
    FillRect(canvas, &ground, green);
    DeleteObject(green);

    HBRUSH yellow = CreateSolidBrush(RGB(255, 186, 71));
    HGDIOBJ oldBrush = SelectObject(canvas, yellow);
    Ellipse(canvas, 191, 16, 232, 57);
    HBRUSH walls = CreateSolidBrush(RGB(245, 234, 207));
    SelectObject(canvas, walls);
    Rectangle(canvas, 43, 84, 135, 135);
    HBRUSH roof = CreateSolidBrush(RGB(161, 71, 64));
    SelectObject(canvas, roof);
    const POINT triangle[] = {{32, 86}, {83, 42}, {147, 86}};
    Polygon(canvas, triangle, ARRAYSIZE(triangle));
    SelectObject(canvas, GetStockObject(DKGRAY_BRUSH));
    Rectangle(canvas, 96, 102, 117, 135);
    SelectObject(canvas, oldBrush);
    DeleteObject(roof);
    DeleteObject(walls);
    DeleteObject(yellow);
}

LRESULT CALLBACK ViewerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == PictureMessage()) {
        visible = true;
        flipped = wParam != 0;
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        if (!visible) {
            DrawTextW(dc, L"Нажмите «Нарисовать» в первом приложении.", -1, &client,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            const HDC image = CreateCompatibleDC(dc);
            const HBITMAP bitmap = CreateCompatibleBitmap(dc, pictureWidth, pictureHeight);
            if (image && bitmap) {
                const HGDIOBJ oldBitmap = SelectObject(image, bitmap);
                DrawPicture(image);
                const int x = std::max(0L, (client.right - pictureWidth) / 2);
                const int y = std::max(0L, (client.bottom - pictureHeight) / 2);
                if (flipped) {
                    StretchBlt(dc, x + pictureWidth, y, -pictureWidth, pictureHeight,
                        image, 0, 0, pictureWidth, pictureHeight, SRCCOPY);
                } else {
                    BitBlt(dc, x, y, pictureWidth, pictureHeight, image, 0, 0, SRCCOPY);
                }
                SelectObject(image, oldBitmap);
            }
            if (bitmap) DeleteObject(bitmap);
            if (image) DeleteDC(image);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    if (!PictureMessage()) return 1;
    WNDCLASSW type{};
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = ViewerProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    type.lpszClassName = viewerClass;
    if (!RegisterClassW(&type)) return 1;
    return WindowLoop(CreateWindowW(viewerClass, L"Практическая №1 — просмотр",
        WS_OVERLAPPEDWINDOW, 455, 100, 410, 280, nullptr, nullptr, instance, nullptr), show);
}
