#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
constexpr UINT_PTR scanTimer = 1;
enum ControlId { processList = 101, programPath, browse, launch, stop, folderPath, monitor, scanStatus };

struct Process {
    DWORD pid;
    DWORD parent;
    std::wstring name;
};

std::vector<Process> processes;
std::unordered_set<DWORD> managedPids;
std::wstring monitorFolder;
HWND list{}, program{}, folder{}, monitorButton{}, state{};
DWORD ownSession{};
bool monitoring = false;

struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring Text(HWND item)
{
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(item)) + 1, L'\0');
    value.resize(GetWindowTextW(item, value.data(), static_cast<int>(value.size())));
    return value;
}

std::wstring ImagePath(HANDLE process)
{
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
    path.resize(length);
    return path;
}

bool Snapshot(std::vector<Process>& result)
{
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) return false;
    do {
        result.push_back({entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
    } while (Process32NextW(snapshot.value, &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
}

DWORD StartExecutable(const std::wstring& path, DWORD& error)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + path + L"\"";
    const auto separator = path.find_last_of(L"\\/");
    const auto workingDirectory = path.substr(0, separator + 1);
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process)) {
        error = GetLastError();
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    managedPids.insert(process.dwProcessId);
    return process.dwProcessId;
}

bool InMonitorFolder(const std::wstring& path)
{
    return monitorFolder.empty() || (path.size() > monitorFolder.size()
        && CompareStringOrdinal(path.c_str(), static_cast<int>(monitorFolder.size()),
            monitorFolder.c_str(), static_cast<int>(monitorFolder.size()), TRUE) == CSTR_EQUAL);
}

DWORD SelectedPid()
{
    const LRESULT index = SendMessageW(list, LB_GETCURSEL, 0, 0);
    return index == LB_ERR ? 0 : static_cast<DWORD>(SendMessageW(list, LB_GETITEMDATA, index, 0));
}

void UpdateList()
{
    std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
        return a.pid < b.pid;
    });
    const DWORD selected = SelectedPid();
    const LRESULT top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& process : processes) {
        const std::wstring row = std::to_wstring(process.pid) + L"\t" + process.name;
        const auto index = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        if (index >= 0) {
            SendMessageW(list, LB_SETITEMDATA, index, process.pid);
            if (process.pid && process.pid == selected) SendMessageW(list, LB_SETCURSEL, index, 0);
        }
    }
    SendMessageW(list, LB_SETTOPINDEX, top, 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void Scan(bool baseline = false)
{
    std::vector<Process> current;
    if (!Snapshot(current)) {
        SetWindowTextW(state, L"Не удалось получить снимок процессов.");
        return;
    }
    std::unordered_set<DWORD> previous;
    for (const auto& process : processes) previous.insert(process.pid);
    const auto existingManaged = managedPids;

    for (const auto& process : current) {
        if (process.parent == GetCurrentProcessId() || existingManaged.count(process.parent)) {
            managedPids.insert(process.pid);
        }
    }
    if (monitoring && !baseline) {
        unsigned detected = 0, copies = 0;
        DWORD error = ERROR_SUCCESS;
        for (const auto& process : current) {
            if (!process.pid || previous.count(process.pid) || managedPids.count(process.pid)) continue;
            DWORD session{};
            if (!ProcessIdToSessionId(process.pid, &session) || session != ownSession) continue;
            Handle original{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid)};
            if (!original.value) continue;
            const auto path = ImagePath(original.value);
            if (path.empty() || !InMonitorFolder(path)) continue;
            ++detected;
            for (int i = 0; i < 2; ++i) {
                if (StartExecutable(path, error)) ++copies;
            }
        }
        if (detected) {
            auto report = L"Внешних запусков: " + std::to_wstring(detected) + L". Создано копий: "
                + std::to_wstring(copies) + L".";
            if (error) report += L" Ошибка Win32: " + std::to_wstring(error);
            SetWindowTextW(state, report.c_str());
        }
    }

    std::unordered_set<DWORD> currentIds;
    for (const auto& process : current) currentIds.insert(process.pid);
    for (const DWORD pid : existingManaged) {
        if (!currentIds.count(pid)) managedPids.erase(pid);
    }
    if (currentIds != previous) {
        processes.swap(current);
        UpdateList();
    }
}

void Browse(HWND window)
{
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Приложения (*.exe)\0*.exe\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) SetWindowTextW(program, path);
}

void Launch()
{
    const auto path = Text(program);
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || _wcsicmp(path.c_str() + dot, L".exe") != 0) {
        SetWindowTextW(state, L"Укажите полный путь к файлу .exe.");
        return;
    }
    DWORD error = ERROR_SUCCESS;
    unsigned launched = 0;
    for (int i = 0; i < 3; ++i) {
        if (!StartExecutable(path, error)) break;
        ++launched;
    }
    auto report = L"Запущено экземпляров: " + std::to_wstring(launched) + L" из 3.";
    if (error) report += L" Ошибка Win32: " + std::to_wstring(error);
    SetWindowTextW(state, report.c_str());
    Scan();
}

void TerminateSelection(HWND window)
{
    const DWORD pid = SelectedPid();
    if (!pid || pid == GetCurrentProcessId()) {
        SetWindowTextW(state, L"Выберите другой процесс в списке.");
        return;
    }
    const auto item = std::find_if(processes.begin(), processes.end(), [pid](const Process& p) { return p.pid == pid; });
    Handle process{OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) {
        const auto report = L"Не удалось открыть процесс. Ошибка Win32: " + std::to_wstring(GetLastError());
        SetWindowTextW(state, report.c_str());
        return;
    }
    const auto path = ImagePath(process.value);
    const auto name = path.substr(path.find_last_of(L"\\/") + 1);
    if (item == processes.end() || _wcsicmp(name.c_str(), item->name.c_str()) != 0) {
        SetWindowTextW(state, L"Выбранный процесс уже изменился. Обновите выбор.");
        return;
    }
    const auto prompt = L"Завершить " + name + L" (PID " + std::to_wstring(pid) + L")?";
    if (MessageBoxW(window, prompt.c_str(), L"Завершение процесса", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
    if (!TerminateProcess(process.value, 0)) {
        const auto report = L"Не удалось завершить процесс. Ошибка Win32: " + std::to_wstring(GetLastError());
        SetWindowTextW(state, report.c_str());
        return;
    }
    SetWindowTextW(state, L"Процесс завершён.");
    Scan();
}

void ToggleMonitor(HWND window)
{
    if (monitoring) {
        monitoring = false;
        EnableWindow(folder, TRUE);
        SetWindowTextW(monitorButton, L"Включить");
        SetWindowTextW(state, L"Наблюдение выключено.");
        return;
    }

    auto choice = Text(folder);
    if (choice.empty()) {
        if (MessageBoxW(window, L"Добавлять две копии новых доступных процессов в текущем сеансе?",
            L"Наблюдение за всем сеансом", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
    } else {
        wchar_t fullPath[32768];
        const DWORD len = GetFullPathNameW(choice.c_str(), ARRAYSIZE(fullPath), fullPath, nullptr);
        const DWORD attributes = len && len < ARRAYSIZE(fullPath) ? GetFileAttributesW(fullPath) : INVALID_FILE_ATTRIBUTES;
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            SetWindowTextW(state, L"Папка наблюдения не найдена.");
            return;
        }
        choice.assign(fullPath, len);
        if (choice.back() != L'\\') choice += L'\\';
    }
    monitorFolder = std::move(choice);
    Scan(true);
    monitoring = true;
    EnableWindow(folder, FALSE);
    SetWindowTextW(monitorButton, L"Выключить");
    SetWindowTextW(state, L"Наблюдение включено. Новые внешние запуски получат две копии.");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        auto control = [&](LPCWSTR type, LPCWSTR label, DWORD style, int x, int y,
            int width, int height, INT_PTR id = 0) {
            const HWND item = CreateWindowW(type, label, WS_CHILD | WS_VISIBLE | style, x, y,
                width, height, window, reinterpret_cast<HMENU>(id), instance, nullptr);
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        control(L"STATIC", L"PID            Имя процесса", 0, 20, 16, 410, 20);
        list = control(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_USETABSTOPS | LBS_NOINTEGRALHEIGHT,
            20, 39, 622, 255, processList);
        constexpr int tab = 60;
        SendMessageW(list, LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tab));
        control(L"BUTTON", L"Завершить выбранный", WS_TABSTOP, 20, 304, 178, 28, stop);
        control(L"STATIC", L"Путь к .exe", 0, 20, 351, 125, 20);
        program = control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 147, 347, 275, 25, programPath);
        control(L"BUTTON", L"Обзор…", WS_TABSTOP, 430, 346, 82, 27, browse);
        control(L"BUTTON", L"Запустить ×3", WS_TABSTOP, 522, 346, 120, 27, launch);
        control(L"STATIC", L"Папка наблюдения", 0, 20, 391, 127, 20);
        folder = control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 147, 387, 365, 25, folderPath);
        control(L"STATIC", L"Пустое поле — весь текущий сеанс Windows.", 0, 147, 419, 480, 20);
        monitorButton = control(L"BUTTON", L"Включить", WS_TABSTOP, 522, 386, 120, 27, monitor);
        state = control(L"STATIC", L"Наблюдение выключено.", 0, 20, 451, 622, 35, scanStatus);
        if (!list || !program || !folder || !monitorButton || !state || !SetTimer(window, scanTimer, 250, nullptr)) return -1;
        Scan(true);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED) break;
        switch (LOWORD(wParam)) {
        case browse: Browse(window); return 0;
        case launch: Launch(); return 0;
        case stop: TerminateSelection(window); return 0;
        case monitor: ToggleMonitor(window); return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == scanTimer) Scan();
        return 0;
    case WM_DESTROY:
        KillTimer(window, scanTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ownSession)) return 1;
    constexpr wchar_t className[] = L"Practice02.ProcessManager";
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = className;
    if (!RegisterClassW(&type)) return 1;
    const HWND window = CreateWindowW(className, L"Практическая №2 — диспетчер задач",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 525, nullptr, nullptr, instance, nullptr);
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
