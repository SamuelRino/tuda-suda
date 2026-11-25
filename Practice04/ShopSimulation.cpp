#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr int capacity = 5, openingThreshold = 4;
constexpr DWORD breakDuration = 3000;
constexpr UINT_PTR timer = 1;
constexpr int startId = 101, stopId = 102;
constexpr wchar_t shopClass[] = L"Practice04.Shop";
enum Phase { gathering, open, onBreak };

struct ShopState {
    Phase phase;
    int seats[capacity];
    int inside, waiting, visited, departed;
    ULONGLONG reopenAt;
};

class Simulation {
public:
    bool Init(const std::wstring& prefix, bool parent)
    {
        const auto mapName = prefix + L".State";
        const auto mutexName = prefix + L".Mutex";
        const auto seatsName = prefix + L".Seats";
        const auto openName = prefix + L".Admission";
        const auto stopName = prefix + L".Stop";

        if (parent) {
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                sizeof(ShopState), mapName.c_str());
            mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
            places = CreateSemaphoreW(nullptr, capacity, capacity, seatsName.c_str());
            admission = CreateEventW(nullptr, TRUE, TRUE, openName.c_str());
            stopped = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
        } else {
            mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, mapName.c_str());
            mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutexName.c_str());
            places = OpenSemaphoreW(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, seatsName.c_str());
            admission = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, openName.c_str());
            stopped = OpenEventW(SYNCHRONIZE, FALSE, stopName.c_str());
        }
        if (!mapping || !mutex || !places || !admission || !stopped) {
            Close();
            return false;
        }
        state = static_cast<ShopState*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(ShopState)));
        if (!state) {
            Close();
            return false;
        }
        if (parent) *state = {};
        return true;
    }

    void Close()
    {
        if (state) {
            UnmapViewOfFile(state);
            state = nullptr;
        }
        for (HANDLE* handle : {&mapping, &mutex, &places, &admission, &stopped}) {
            if (*handle) {
                CloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }

    ~Simulation() { Close(); }

    ShopState* state{};
    HANDLE mapping{}, mutex{}, places{}, admission{}, stopped{};
};

struct Section {
    explicit Section(HANDLE handle) : mutex(handle) { WaitForSingleObject(mutex, INFINITE); }
    ~Section() { ReleaseMutex(mutex); }
    HANDLE mutex;
};

int RunCustomer(const std::wstring& prefix, int id, DWORD stay)
{
    Simulation shop;
    if (!shop.Init(prefix, false)) return 1;
    {
        Section lock(shop.mutex);
        ++shop.state->waiting;
    }

    const HANDLE door[]{shop.stopped, shop.admission};
    const HANDLE chair[]{shop.stopped, shop.places};
    int place = -1;
    while (WaitForMultipleObjects(ARRAYSIZE(door), door, FALSE, INFINITE) == WAIT_OBJECT_0 + 1
        && WaitForMultipleObjects(ARRAYSIZE(chair), chair, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        Section lock(shop.mutex);
        if (shop.state->phase == onBreak || WaitForSingleObject(shop.stopped, 0) != WAIT_TIMEOUT) {
            ReleaseSemaphore(shop.places, 1, nullptr);
            continue;
        }
        for (int i = 0; i < capacity; ++i) {
            if (!shop.state->seats[i]) {
                place = i;
                break;
            }
        }
        if (place < 0) {
            ReleaseSemaphore(shop.places, 1, nullptr);
            break;
        }
        shop.state->seats[place] = id;
        --shop.state->waiting;
        ++shop.state->visited;
        if (++shop.state->inside >= openingThreshold) shop.state->phase = open;
        break;
    }

    if (place < 0) {
        Section lock(shop.mutex);
        --shop.state->waiting;
        return 0;
    }

    WaitForSingleObject(shop.stopped, stay);
    {
        Section lock(shop.mutex);
        shop.state->seats[place] = 0;
        --shop.state->inside;
        ++shop.state->departed;
        if (shop.state->phase == open && shop.state->inside < openingThreshold) {
            shop.state->phase = onBreak;
            shop.state->reopenAt = GetTickCount64() + breakDuration;
            ResetEvent(shop.admission);
        }
        ReleaseSemaphore(shop.places, 1, nullptr);
    }
    return 0;
}

Simulation shop;
ShopState display{};
HWND startButton{}, stopButton{}, note{};
std::wstring modulePath, objectPrefix;
std::vector<HANDLE> customers;
std::mt19937 randomEngine{std::random_device{}()};
std::uniform_int_distribution<DWORD> visitLength{1, 8};
ULONGLONG nextArrival{};
int nextId = 1;
bool active = false;

bool CreateCustomer()
{
    const DWORD stay = visitLength(randomEngine) * 1000;
    std::wstring command = L"\"" + modulePath + L"\" --customer " + objectPrefix + L" "
        + std::to_wstring(nextId) + L" " + std::to_wstring(stay);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(modulePath.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &startup, &process)) return false;
    ++nextId;
    CloseHandle(process.hThread);
    customers.push_back(process.hProcess);
    return true;
}

void Start()
{
    if (active || !customers.empty()) return;
    objectPrefix = L"Local\\Practice04." + std::to_wstring(GetCurrentProcessId()) + L"."
        + std::to_wstring(GetTickCount64());
    if (!shop.Init(objectPrefix, true)) {
        SetWindowTextW(note, L"Не удалось создать объекты синхронизации.");
        return;
    }
    display = {};
    nextId = 1;
    active = true;
    nextArrival = GetTickCount64();
    EnableWindow(startButton, FALSE);
    EnableWindow(stopButton, TRUE);
    SetWindowTextW(note, L"Для открытия нужно 4 покупателя. Вместимость — 5 человек.");
}

void Stop()
{
    active = false;
    if (shop.stopped) SetEvent(shop.stopped);
    EnableWindow(stopButton, FALSE);
}

void Tick(HWND window)
{
    customers.erase(std::remove_if(customers.begin(), customers.end(), [](HANDLE process) {
        if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(process);
        return true;
    }), customers.end());

    const ULONGLONG now = GetTickCount64();
    if (shop.state) {
        Section lock(shop.mutex);
        if (active && shop.state->phase == onBreak && now >= shop.state->reopenAt) {
            shop.state->phase = gathering;
            SetEvent(shop.admission);
        }
        display = *shop.state;
    }
    if (active && now >= nextArrival && customers.size() < 18) {
        if (!CreateCustomer()) {
            const auto error = L"Не удалось запустить покупателя. Ошибка Win32: " + std::to_wstring(GetLastError());
            SetWindowTextW(note, error.c_str());
            Stop();
        }
        nextArrival = now + 500;
    }
    if (!active && shop.state && customers.empty()) {
        shop.Close();
        EnableWindow(startButton, TRUE);
    }
    RECT diagram{0, 52, 650, 300};
    InvalidateRect(window, &diagram, FALSE);
}

void DrawShop(HDC dc)
{
    RECT diagram{0, 52, 650, 300};
    FillRect(dc, &diagram, GetSysColorBrush(COLOR_BTNFACE));
    const HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    std::wstring title = L"Магазин остановлен";
    COLORREF phaseColor = RGB(90, 92, 97);
    if (active) {
        if (display.phase == open) {
            title = L"Магазин открыт";
            phaseColor = RGB(19, 106, 65);
        } else if (display.phase == onBreak) {
            title = L"Перерыв";
            phaseColor = RGB(160, 51, 41);
        } else {
            title = L"Сбор покупателей до открытия";
            phaseColor = RGB(129, 86, 24);
        }
    }
    SetTextColor(dc, phaseColor);
    TextOutW(dc, 20, 68, title.c_str(), static_cast<int>(title.size()));
    SetTextColor(dc, RGB(25, 30, 38));
    for (int i = 0; i < capacity; ++i) {
        const int x = 20 + i * 122;
        RECT tile{x, 111, x + 111, 181};
        const int id = display.seats[i];
        const HBRUSH fill = CreateSolidBrush(id ? RGB(204, 231, 210) : RGB(229, 233, 239));
        FillRect(dc, &tile, fill);
        DeleteObject(fill);
        const auto caption = id ? L"Клиент " + std::to_wstring(id) : std::wstring(L"Свободно");
        DrawTextW(dc, caption.c_str(), -1, &tile, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const auto count = L"Внутри: " + std::to_wstring(display.inside) + L" из 5    Ожидают: "
        + std::to_wstring(display.waiting) + L"    Вошли: " + std::to_wstring(display.visited)
        + L"    Ушли: " + std::to_wstring(display.departed);
    TextOutW(dc, 20, 211, count.c_str(), static_cast<int>(count.size()));
    if (active && display.phase == onBreak) {
        const auto now = GetTickCount64();
        const auto seconds = now < display.reopenAt ? (display.reopenAt - now + 999) / 1000 : 0;
        const auto time = L"До конца перерыва: " + std::to_wstring(seconds) + L" с";
        TextOutW(dc, 20, 241, time.c_str(), static_cast<int>(time.size()));
    }
    SelectObject(dc, oldFont);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        auto control = [&](LPCWSTR type, LPCWSTR label, int x, int y,
            int width, int height, INT_PTR id = 0) {
            const HWND item = CreateWindowW(type, label, WS_CHILD | WS_VISIBLE | (id ? WS_TABSTOP : 0),
                x, y, width, height, window, reinterpret_cast<HMENU>(id), instance, nullptr);
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        startButton = control(L"BUTTON", L"Начать", 20, 15, 115, 29, startId);
        stopButton = control(L"BUTTON", L"Остановить", 147, 15, 115, 29, stopId);
        note = control(L"STATIC", L"", 20, 305, 610, 30);
        EnableWindow(stopButton, FALSE);
        return startButton && stopButton && note && SetTimer(window, timer, 100, nullptr) ? 0 : -1;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED) break;
        if (LOWORD(wParam) == startId) Start();
        if (LOWORD(wParam) == stopId) Stop();
        return 0;
    case WM_TIMER:
        if (wParam == timer) Tick(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        DrawShop(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, timer);
        Stop();
        for (HANDLE customer : customers) {
            WaitForSingleObject(customer, 1000);
            CloseHandle(customer);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    int argc{};
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    if (argc == 5 && std::wcscmp(argv[1], L"--customer") == 0) {
        const std::wstring prefix = argv[2];
        const int id = _wtoi(argv[3]);
        const int stay = _wtoi(argv[4]);
        LocalFree(argv);
        return id > 0 && stay >= 1000 && stay <= 8000 ? RunCustomer(prefix, id, stay) : 1;
    }
    LocalFree(argv);
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return 1;
    modulePath.assign(path, length);
    customers.reserve(18);

    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = shopClass;
    if (!RegisterClassW(&type)) return 1;
    const HWND window = CreateWindowW(shopClass, L"Практическая №4 — магазин",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 378, nullptr, nullptr, instance, nullptr);
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
