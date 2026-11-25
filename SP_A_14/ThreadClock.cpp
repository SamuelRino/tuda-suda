#define NOMINMAX
#include <windows.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
constexpr int priorityId = 101, benchmarkId = 102;
constexpr UINT_PTR paintTimer = 1, logTimer = 2;
constexpr double fullTurn = 6.283185307179586;
const wchar_t clockClass[] = L"Practice03.ThreadClock";
HWND priorityBox{}, status{}, logStatus{};
std::filesystem::path logPath;

enum class Hand : size_t { hour, minute, second };

double HandAngle(Hand hand, const SYSTEMTIME& time)
{
    const double seconds = time.wSecond + time.wMilliseconds / 1000.0;
    const double minutes = time.wMinute + seconds / 60.0;
    switch (hand) {
    case Hand::hour: return (time.wHour % 12 + minutes / 60.0) * fullTurn / 12.0;
    case Hand::minute: return minutes * fullTurn / 60.0;
    case Hand::second: return seconds * fullTurn / 60.0;
    }
    return 0.0;
}

class HandWorker {
public:
    bool Start(Hand kind, HANDLE stop)
    {
        kind_ = kind;
        stop_ = stop;
        request_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        done_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!request_ || !done_) return false;
        thread_ = CreateThread(nullptr, 0, Run, this, 0, nullptr);
        return thread_ != nullptr;
    }

    ~HandWorker()
    {
        if (thread_) {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (request_) CloseHandle(request_);
        if (done_) CloseHandle(done_);
    }

    void Request(const SYSTEMTIME& time)
    {
        time_ = time;
        SetEvent(request_);
    }

    HANDLE Done() const { return done_; }
    double Angle() const { return angle_; }
    bool SetPriority(int priority) { return SetThreadPriority(thread_, priority) != FALSE; }

private:
    static DWORD WINAPI Run(void* argument)
    {
        auto& worker = *static_cast<HandWorker*>(argument);
        const HANDLE signals[]{worker.stop_, worker.request_};
        while (WaitForMultipleObjects(2, signals, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            worker.angle_ = HandAngle(worker.kind_, worker.time_);
            SetEvent(worker.done_);
        }
        return 0;
    }

    HANDLE stop_{}, request_{}, done_{}, thread_{};
    Hand kind_{};
    SYSTEMTIME time_{};
    double angle_{};
};

class ClockWorkers {
public:
    bool Start()
    {
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) return false;
        for (size_t i = 0; i < hands_.size(); ++i) {
            if (!hands_[i].Start(static_cast<Hand>(i), stop_)) return false;
        }
        return true;
    }

    void Stop() { if (stop_) SetEvent(stop_); }

    std::array<double, 3> Update(const SYSTEMTIME& time)
    {
        const HANDLE done[]{hands_[0].Done(), hands_[1].Done(), hands_[2].Done()};
        for (auto& hand : hands_) hand.Request(time);
        WaitForMultipleObjects(ARRAYSIZE(done), done, TRUE, INFINITE);
        return {hands_[0].Angle(), hands_[1].Angle(), hands_[2].Angle()};
    }

    bool SetPriority(int priority)
    {
        bool ok = true;
        for (auto& hand : hands_) {
            if (!hand.SetPriority(priority)) ok = false;
        }
        return ok;
    }

    ~ClockWorkers() { Stop(); }

private:
    // Объект события живёт до завершения потоков.
    struct StopEvent {
        HANDLE value{};
        operator HANDLE() const { return value; }
        StopEvent& operator=(HANDLE newValue) { value = newValue; return *this; }
        ~StopEvent() { if (value) CloseHandle(value); }
    } stop_;
    std::array<HandWorker, 3> hands_{};
};

ClockWorkers workers;
std::array<double, 3> angles{};

void WriteTime()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::ofstream file(logPath, std::ios::app);
    file << std::setfill('0') << std::setw(4) << time.wYear << '-'
         << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay << ' '
         << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute << ':'
         << std::setw(2) << time.wSecond << '\n';
    file.close();
    SetWindowTextW(logStatus, file ? L"Журнал clock.log обновлён." : L"Не удалось записать clock.log.");
}

void Compare()
{
    const auto selected = SendMessageW(priorityBox, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return;
    constexpr int priorities[]{THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL,
        THREAD_PRIORITY_ABOVE_NORMAL};
    if (!workers.SetPriority(priorities[selected])) {
        SetWindowTextW(status, L"Не удалось установить приоритет.");
        return;
    }

    using Timer = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    std::array<SYSTEMTIME, 500> samples{};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = now;
        samples[i].wMilliseconds = static_cast<WORD>(i * 2);
    }

    auto start = Timer::now();
    double serialSum = 0;
    for (const auto& sample : samples) {
        serialSum += HandAngle(Hand::hour, sample) + HandAngle(Hand::minute, sample)
            + HandAngle(Hand::second, sample);
    }
    const double serial = Milliseconds(Timer::now() - start).count();
    start = Timer::now();
    double parallelSum = 0;
    for (const auto& sample : samples) {
        const auto values = workers.Update(sample);
        parallelSum += values[0] + values[1] + values[2];
    }
    const double threaded = Milliseconds(Timer::now() - start).count();

    std::wostringstream text;
    text << std::fixed << std::setprecision(3)
        << L"500 кадров: последовательно " << serial << L" мс, три потока " << threaded << L" мс.\r\n"
        << (serialSum == parallelSum ? L"Результаты совпали." : L"Результаты различаются.");
    SetWindowTextW(status, text.str().c_str());
}

void DrawClock(HDC dc)
{
    constexpr int x = 245, y = 180, radius = 151;
    Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
    for (int i = 0; i < 60; ++i) {
        const double angle = fullTurn * i / 60.0;
        const int inner = i % 5 == 0 ? radius - 15 : radius - 8;
        MoveToEx(dc, x + static_cast<int>(inner * std::sin(angle)),
            y - static_cast<int>(inner * std::cos(angle)), nullptr);
        LineTo(dc, x + static_cast<int>((radius - 4) * std::sin(angle)),
            y - static_cast<int>((radius - 4) * std::cos(angle)));
    }

    constexpr int lengths[]{68, 105, 124};
    constexpr int widths[]{6, 4, 2};
    for (size_t hand = 0; hand < angles.size(); ++hand) {
        HPEN pen = CreatePen(PS_SOLID, widths[hand], hand == 2 ? RGB(188, 35, 45) : RGB(35, 47, 65));
        const HGDIOBJ previous = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, x + static_cast<int>(lengths[hand] * std::sin(angles[hand])),
            y - static_cast<int>(lengths[hand] * std::cos(angles[hand])));
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        auto control = [&](const wchar_t* cls, const wchar_t* label, DWORD style,
            int x, int y, int width, int height, INT_PTR id = 0) {
            const HWND item = CreateWindowW(cls, label, WS_CHILD | WS_VISIBLE | style,
                x, y, width, height, window, reinterpret_cast<HMENU>(id), instance, nullptr);
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        control(L"STATIC", L"Приоритет потоков", 0, 20, 356, 135, 25);
        priorityBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 155, 351, 145, 125, priorityId);
        for (const wchar_t* item : {L"Ниже среднего", L"Обычный", L"Выше среднего"}) {
            SendMessageW(priorityBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        SendMessageW(priorityBox, CB_SETCURSEL, 1, 0);
        control(L"BUTTON", L"Сравнить", WS_TABSTOP, 320, 349, 140, 31, benchmarkId);
        status = control(L"STATIC", L"", 0, 20, 397, 450, 44);
        logStatus = control(L"STATIC", L"Время записывается в clock.log каждые 10 с.", 0, 20, 443, 450, 22);
        return priorityBox && status && logStatus && SetTimer(window, paintTimer, 40, nullptr)
            && SetTimer(window, logTimer, 10000, nullptr) ? 0 : -1;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == benchmarkId && HIWORD(wParam) == BN_CLICKED) Compare();
        if (LOWORD(wParam) == priorityId && HIWORD(wParam) == CBN_SELCHANGE) {
            constexpr int priorities[]{THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL,
                THREAD_PRIORITY_ABOVE_NORMAL};
            const auto selected = SendMessageW(priorityBox, CB_GETCURSEL, 0, 0);
            if (selected != CB_ERR) {
                SetWindowTextW(status, workers.SetPriority(priorities[selected]) ? L"" : L"Не удалось установить приоритет.");
            }
        }
        return 0;
    case WM_TIMER:
        if (wParam == logTimer) {
            WriteTime();
        } else if (wParam == paintTimer) {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            angles = workers.Update(time);
            RECT face{0, 0, 490, 342};
            InvalidateRect(window, &face, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        DrawClock(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, paintTimer);
        KillTimer(window, logTimer);
        workers.Stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path) || !workers.Start()) {
        MessageBoxW(nullptr, L"Не удалось подготовить журнал или создать три потока.", L"Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }
    logPath = std::filesystem::path(path).parent_path() / L"clock.log";
    SYSTEMTIME now{};
    GetLocalTime(&now);
    angles = workers.Update(now);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = clockClass;
    if (!RegisterClassW(&type)) return 1;
    const HWND window = CreateWindowW(clockClass, L"Практическая №3 — механические часы",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 505, 520, nullptr, nullptr, instance, nullptr);
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
