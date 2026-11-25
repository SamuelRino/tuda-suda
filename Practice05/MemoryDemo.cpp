#include "MemoryBlock.h"
#include <cwchar>
#include <string>

namespace {
constexpr int maxText = 4095;
constexpr size_t capacity = maxText + 1;
enum ControlId {
    selectBlock = 101, allocate, lock, unlock, write, read, copy, freeBlock,
    clipboardOut, clipboardIn, editorId
};

MemoryBlock blocks[2]{};
HWND picker{}, editor{}, blockStatus{}, result{};

std::wstring EditorText()
{
    wchar_t text[capacity]{};
    GetWindowTextW(editor, text, ARRAYSIZE(text));
    return text;
}

bool ToClipboard(HWND window, const std::wstring& text)
{
    const size_t size = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return false;
    auto* data = static_cast<wchar_t*>(GlobalLock(memory));
    if (!data) {
        GlobalFree(memory);
        return false;
    }
    std::wmemcpy(data, text.c_str(), text.size() + 1);
    GlobalUnlock(memory);
    bool ok = false;
    if (OpenClipboard(window)) {
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
    }
    if (!ok) GlobalFree(memory);
    return ok;
}

bool FromClipboard(HWND window)
{
    if (!OpenClipboard(window)) return false;
    const HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    bool ok = false;
    if (memory) {
        const auto* data = static_cast<const wchar_t*>(GlobalLock(memory));
        if (data) {
            const size_t limit = GlobalSize(memory) / sizeof(wchar_t);
            const wchar_t* end = std::wmemchr(data, L'\0', limit);
            if (end && end - data <= maxText) ok = SetWindowTextW(editor, data) != FALSE;
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    return ok;
}

void ShowBlocks()
{
    auto state = [](const MemoryBlock& block) {
        if (block.data) return L"заблокирован";
        return block.handle ? L"выделен" : L"свободен";
    };
    const std::wstring text = std::wstring(L"Блок A: ") + state(blocks[0]) + L"     Блок B: " + state(blocks[1]);
    SetWindowTextW(blockStatus, text.c_str());
}

const wchar_t* Perform(HWND window, int action, MemoryBlock& active)
{
    switch (action) {
    case allocate:
        return AllocateBlock(active, capacity) ? L"Блок выделен (4096 позиций UTF-16)."
            : L"Блок уже выделен или нет памяти.";
    case lock:
        return LockBlock(active) ? L"Блок заблокирован. Можно читать и записывать."
            : L"Нужен выделенный и разблокированный блок.";
    case unlock:
        return UnlockBlock(active) ? L"Блок разблокирован." : L"Блок не удалось разблокировать.";
    case write:
        return WriteBlock(active, EditorText().c_str()) ? L"Текст записан в блок."
            : L"Для записи нужен заблокированный блок.";
    case read:
        if (const wchar_t* text = ReadBlock(active)) {
            SetWindowTextW(editor, text);
            return L"Текст считан из блока.";
        }
        return L"Для чтения нужен заблокированный блок.";
    case copy:
        return CopyBlock(blocks[0], blocks[1]) ? L"Данные из A скопированы в B."
            : L"Для копирования заблокируйте A и B.";
    case freeBlock:
        return FreeBlock(active) ? L"Блок освобождён." : L"Выделите блок или сначала разблокируйте его.";
    case clipboardOut:
        return ToClipboard(window, EditorText()) ? L"Текст скопирован в буфер обмена."
            : L"Не удалось записать текст в буфер обмена.";
    case clipboardIn:
        return FromClipboard(window) ? L"Текст вставлен из буфера обмена."
            : L"Буфер недоступен или в нём нет текста длиной до 4095 символов.";
    default:
        return L"";
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        auto control = [&](LPCWSTR type, LPCWSTR label, DWORD style, int x, int y,
            int width, int height, INT_PTR id = 0) {
            const auto item = CreateWindowW(type, label, WS_CHILD | WS_VISIBLE | style, x, y,
                width, height, window, reinterpret_cast<HMENU>(id), instance, nullptr);
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        control(L"STATIC", L"Рабочий блок", 0, 20, 23, 100, 23);
        picker = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 123, 19, 84, 105, selectBlock);
        SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"A"));
        SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"B"));
        SendMessageW(picker, CB_SETCURSEL, 0, 0);
        blockStatus = control(L"STATIC", L"", 0, 226, 23, 355, 25);
        editor = control(L"EDIT", L"Пример текста для блока A", WS_BORDER | WS_TABSTOP
            | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 20, 60, 560, 105, editorId);
        SendMessageW(editor, EM_SETLIMITTEXT, maxText, 0);
        struct Button { int id; const wchar_t* text; };
        const Button buttons[]{
            {allocate, L"Выделить"}, {lock, L"Заблокировать"}, {unlock, L"Разблокировать"},
            {freeBlock, L"Освободить"}, {write, L"Записать"}, {read, L"Прочитать"},
            {copy, L"Скопировать A → B"}
        };
        for (int i = 0; i < ARRAYSIZE(buttons); ++i) {
            control(L"BUTTON", buttons[i].text, WS_TABSTOP, 20 + (i % 4) * 142, 181 + (i / 4) * 39,
                134, 31, buttons[i].id);
        }
        control(L"BUTTON", L"В буфер обмена", WS_TABSTOP, 20, 263, 170, 30, clipboardOut);
        control(L"BUTTON", L"Из буфера обмена", WS_TABSTOP, 202, 263, 170, 30, clipboardIn);
        result = control(L"STATIC", L"", 0, 20, 312, 560, 33);
        ShowBlocks();
        return picker && editor && blockStatus && result ? 0 : -1;
    }
    case WM_COMMAND: {
        const int action = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED || action < allocate || action > clipboardIn) break;
        const auto selected = SendMessageW(picker, CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR) {
            SetWindowTextW(result, Perform(window, action, blocks[selected]));
            ShowBlocks();
        }
        return 0;
    }
    case WM_DESTROY:
        for (auto& block : blocks) {
            if (block.data) UnlockBlock(block);
            if (block.handle) FreeBlock(block);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    constexpr wchar_t name[] = L"Practice05.LocalMemory";
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = name;
    if (!RegisterClassW(&type)) return 1;
    const HWND window = CreateWindowW(name, L"Практическая №5 — локальная память",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 615, 395, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{};
    BOOL received;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return received == -1 ? 1 : static_cast<int>(message.wParam);
}
