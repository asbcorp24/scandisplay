#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <commctrl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kMainWindowClass[] = L"ScanDisplayMainWindow";
constexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";
constexpr wchar_t kRecordingTitleWindowClass[] = L"ScanDisplayRecordingTitleWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kFinalizeMessage = WM_APP + 2;
constexpr UINT kCaptureFailedMessage = WM_APP + 3;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kTrayStatusTimerId = 1;
constexpr UINT kTrayStatusIntervalMs = 1000;

constexpr UINT kMenuAuth = 1001;
constexpr UINT kMenuStart = 1002;
constexpr UINT kMenuStop = 1003;
constexpr UINT kMenuExit = 1004;
constexpr UINT kAuthEditId = 2001;
constexpr UINT kAuthButtonId = 2002;
constexpr UINT kRecordingTitleEditId = 2101;
constexpr UINT kRecordingTitleButtonId = 2102;

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HWND g_authWindow = nullptr;
HWND g_authEdit = nullptr;
HWND g_authStatus = nullptr;
HWND g_recordingTitleWindow = nullptr;
HWND g_recordingTitleEdit = nullptr;
HWND g_recordingTitleStatus = nullptr;
NOTIFYICONDATAW g_tray{};

struct Config {
    std::wstring baseUrl;
    std::wstring ffmpegPath;
    fs::path outputDir;
    bool deleteAfterUpload = false;
    int captureIntervalSeconds = 30;
    int videoMaxWidth = 1280;
    int videoCrf = 35;
    std::wstring videoPreset = L"veryslow";
    DWORD timeoutMs = 120000;
};

struct StudentSession {
    int id = 0;
    std::wstring code;
    std::wstring firstName;
    std::wstring lastName;
    std::wstring groupName;
    std::wstring token;

    bool authorized() const { return id > 0 && !token.empty(); }
    std::wstring fullName() const { return lastName + L" " + firstName; }
};

enum class AppState { Idle, Recording, Finalizing };

Config g_config;
bool g_configCreated = false;
StudentSession g_student;
StudentSession g_recordingStudent;
std::mutex g_studentMutex;
std::atomic<AppState> g_state{AppState::Idle};
std::atomic_bool g_stopCapture{false};
std::thread g_captureThread;
std::thread g_finalizeThread;
std::condition_variable g_captureWake;
std::mutex g_captureWakeMutex;
std::mutex g_captureErrorMutex;
std::wstring g_captureError;

HANDLE g_ffmpegInput = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ffmpegProcess{};
fs::path g_outputFile;
std::wstring g_startedAt;
std::wstring g_recordingTitle;
std::chrono::steady_clock::time_point g_recordingStartedSteady{};
int g_screenX = 0;
int g_screenY = 0;
int g_screenWidth = 0;
int g_screenHeight = 0;

std::wstring ModuleDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return fs::path(std::wstring(buffer.data(), length)).parent_path().wstring();
}

std::wstring TrimText(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring ExpandEnvironment(const std::wstring& value) {
    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) return value;
    std::vector<wchar_t> buffer(required);
    if (ExpandEnvironmentStringsW(value.c_str(), buffer.data(), required) == 0) return value;
    return buffer.data();
}

std::wstring ReadIni(const fs::path& file, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
    return buffer.data();
}

bool CreateDefaultConfig(const fs::path& configPath, std::wstring& error) {
    const auto write = [&](const wchar_t* section, const wchar_t* key, const wchar_t* value) {
        return WritePrivateProfileStringW(section, key, value, configPath.c_str()) != FALSE;
    };

    const bool ok =
        write(L"server", L"base_url", L"http://127.0.0.1/scandisplay/server") &&
        write(L"recording", L"ffmpeg_path", L"ffmpeg.exe") &&
        write(L"recording", L"output_dir", L"%LOCALAPPDATA%\\ScanDisplay\\recordings") &&
        write(L"recording", L"capture_interval_seconds", L"30") &&
        write(L"recording", L"video_max_width", L"1280") &&
        write(L"recording", L"video_crf", L"35") &&
        write(L"recording", L"video_preset", L"veryslow") &&
        write(L"recording", L"delete_after_upload", L"0") &&
        write(L"client", L"request_timeout_seconds", L"120");

    WritePrivateProfileStringW(nullptr, nullptr, nullptr, configPath.c_str());
    if (!ok || !fs::exists(configPath)) {
        error = L"Не удалось создать config.ini рядом с ScanDisplay.exe. Проверьте права на запись в папку программы.";
        return false;
    }
    return true;
}

bool LoadConfig(std::wstring& error) {
    const fs::path configPath = fs::path(ModuleDirectory()) / L"config.ini";

    std::error_code obsoleteAdminError;
    fs::remove(fs::path(ModuleDirectory()) / L"admin.ini", obsoleteAdminError);

    if (!fs::exists(configPath)) {
        if (!CreateDefaultConfig(configPath, error)) return false;
        g_configCreated = true;
    }

    g_config.baseUrl = ReadIni(configPath, L"server", L"base_url", L"");
    const fs::path configuredFfmpeg = ExpandEnvironment(
        ReadIni(configPath, L"recording", L"ffmpeg_path", L"ffmpeg.exe"));
    g_config.ffmpegPath = (configuredFfmpeg.is_absolute()
        ? configuredFfmpeg
        : fs::path(ModuleDirectory()) / configuredFfmpeg).lexically_normal().wstring();
    g_config.outputDir = ExpandEnvironment(ReadIni(configPath, L"recording", L"output_dir", L"%LOCALAPPDATA%\\ScanDisplay\\recordings"));
    g_config.deleteAfterUpload = ReadIni(configPath, L"recording", L"delete_after_upload", L"0") == L"1";

    try {
        const int interval = std::stoi(ReadIni(configPath, L"recording", L"capture_interval_seconds", L"30"));
        g_config.captureIntervalSeconds = interval < 1 ? 1 : interval;
    } catch (...) {
        g_config.captureIntervalSeconds = 30;
    }

    try {
        const int width = std::stoi(ReadIni(configPath, L"recording", L"video_max_width", L"1280"));
        g_config.videoMaxWidth = width < 320 ? 320 : width;
    } catch (...) {
        g_config.videoMaxWidth = 1280;
    }

    try {
        const int crf = std::stoi(ReadIni(configPath, L"recording", L"video_crf", L"35"));
        g_config.videoCrf = max(18, min(45, crf));
    } catch (...) {
        g_config.videoCrf = 35;
    }

    g_config.videoPreset = ReadIni(configPath, L"recording", L"video_preset", L"veryslow");
    if (g_config.videoPreset != L"medium" && g_config.videoPreset != L"slow" &&
        g_config.videoPreset != L"slower" && g_config.videoPreset != L"veryslow") {
        g_config.videoPreset = L"veryslow";
    }

    try {
        const int seconds = std::stoi(ReadIni(configPath, L"client", L"request_timeout_seconds", L"120"));
        g_config.timeoutMs = static_cast<DWORD>((seconds < 10 ? 10 : seconds) * 1000);
    } catch (...) {
        g_config.timeoutMs = 120000;
    }

    while (!g_config.baseUrl.empty() && g_config.baseUrl.back() == L'/') g_config.baseUrl.pop_back();
    if (g_config.baseUrl.empty()) {
        error = L"В config.ini не задан server.base_url.";
        return false;
    }
    if (!fs::exists(g_config.ffmpegPath)) {
        error = (g_configCreated ? L"config.ini создан автоматически рядом с программой.\n\n" : L"") +
            L"FFmpeg не найден: " + g_config.ffmpegPath +
            L"\nПоложите ffmpeg.exe рядом с ScanDisplay.exe либо измените recording.ffmpeg_path.";
        return false;
    }

    std::error_code ec;
    fs::create_directories(g_config.outputDir, ec);
    if (ec) {
        error = L"Не удалось создать каталог записей: " + g_config.outputDir.wstring();
        return false;
    }
    return true;
}

fs::path SessionFile() {
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    fs::path dir = length > 0 ? fs::path(localAppData) / L"ScanDisplay" : fs::path(ModuleDirectory());
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"session.ini";
}

void SaveSession(const StudentSession& session) {
    const fs::path file = SessionFile();
    WritePrivateProfileStringW(L"student", L"id", std::to_wstring(session.id).c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"code", session.code.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"first_name", session.firstName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"last_name", session.lastName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"group_name", session.groupName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"token", session.token.c_str(), file.c_str());
}

void LoadSession() {
    const fs::path file = SessionFile();
    if (!fs::exists(file)) return;

    StudentSession loaded;
    try { loaded.id = std::stoi(ReadIni(file, L"student", L"id", L"0")); } catch (...) { loaded.id = 0; }
    loaded.code = ReadIni(file, L"student", L"code", L"");
    loaded.firstName = ReadIni(file, L"student", L"first_name", L"");
    loaded.lastName = ReadIni(file, L"student", L"last_name", L"");
    loaded.groupName = ReadIni(file, L"student", L"group_name", L"");
    loaded.token = ReadIni(file, L"student", L"token", L"");

    std::lock_guard<std::mutex> lock(g_studentMutex);
    g_student = std::move(loaded);
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}


std::string UrlEncode(const std::string& value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            output << static_cast<char>(c);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return output.str();
}

std::wstring JoinUrl(const std::wstring& base, const wchar_t* endpoint) {
    if (!endpoint || !*endpoint) return base;
    return endpoint[0] == L'/' ? base + endpoint : base + L"/" + endpoint;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

bool ParseUrl(const std::wstring& url, ParsedUrl& parsed) {
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;

    parsed.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    parsed.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) parsed.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (parsed.path.empty()) parsed.path = L"/";
    parsed.port = parts.nPort;
    parsed.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::wstring error;
};

HttpResponse HttpPost(const std::wstring& url, const std::wstring& headers, const std::vector<std::uint8_t>& body) {
    HttpResponse response;
    ParsedUrl parsed;
    if (!ParseUrl(url, parsed)) {
        response.error = L"Некорректный URL сервера.";
        return response;
    }

    HINTERNET session = WinHttpOpen(L"ScanDisplay/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = L"WinHttpOpen: " + std::to_wstring(GetLastError());
        return response;
    }
    WinHttpSetTimeouts(session, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs);

    HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;

    if (!connection || !request) {
        response.error = L"Не удалось создать HTTP-запрос: " + std::to_wstring(GetLastError());
    } else {
        const BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<std::uint8_t*>(body.data()),
            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
        if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
            response.error = L"Ошибка связи с сервером: " + std::to_wstring(GetLastError());
        } else {
            DWORD size = sizeof(response.status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &response.status, &size, nullptr);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                const size_t offset = response.body.size();
                response.body.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, response.body.data() + offset, available, &read)) break;
                response.body.resize(offset + read);
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

std::string JsonString(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;

    std::string result;
    bool escape = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escape) {
            switch (c) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            result.push_back(c);
        }
    }
    return result;
}

int JsonInt(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    try { return std::stoi(json.substr(pos)); } catch (...) { return 0; }
}

std::wstring ComputerName() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(name, &size) ? std::wstring(name, size) : L"unknown";
}

bool Authenticate(const std::wstring& code, std::wstring& error) {
    const std::string form = "code=" + UrlEncode(WideToUtf8(code)) +
        "&computer_name=" + UrlEncode(WideToUtf8(ComputerName()));
    const std::vector<std::uint8_t> body(form.begin(), form.end());
    const HttpResponse response = HttpPost(JoinUrl(g_config.baseUrl, L"/api/auth.php"),
        L"Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n", body);

    if (!response.error.empty()) {
        error = response.error;
        return false;
    }
    if (response.status != 200 || response.body.find("\"ok\":true") == std::string::npos) {
        const std::string message = JsonString(response.body, "message");
        error = message.empty() ? L"Сервер отклонил авторизацию." : Utf8ToWide(message);
        return false;
    }

    StudentSession session;
    session.id = JsonInt(response.body, "id");
    session.code = code;
    session.firstName = Utf8ToWide(JsonString(response.body, "first_name"));
    session.lastName = Utf8ToWide(JsonString(response.body, "last_name"));
    session.groupName = Utf8ToWide(JsonString(response.body, "group_name"));
    session.token = Utf8ToWide(JsonString(response.body, "token"));
    if (!session.authorized()) {
        error = L"Сервер вернул неполные данные студента.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        g_student = session;
    }
    SaveSession(session);
    return true;
}

std::wstring TimestampIso() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[64]{};
    swprintf_s(buffer, _countof(buffer), L"%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::wstring TimestampFolder() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[64]{};
    swprintf_s(buffer, _countof(buffer), L"%04u%02u%02u_%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::wstring TimestampOverlay() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[64]{};
    swprintf_s(buffer, _countof(buffer), L"%02u.%02u.%04u %02u:%02u:%02u",
        st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

void SetCaptureError(const std::wstring& error) {
    std::lock_guard<std::mutex> lock(g_captureErrorMutex);
    if (g_captureError.empty()) g_captureError = error;
}

std::wstring GetCaptureError() {
    std::lock_guard<std::mutex> lock(g_captureErrorMutex);
    return g_captureError;
}

bool WriteHandleAll(HANDLE handle, const void* data, std::uint64_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t total = 0;
    while (total < size) {
        const DWORD part = static_cast<DWORD>(min<std::uint64_t>(size - total, 1024ULL * 1024ULL));
        DWORD written = 0;
        if (!WriteFile(handle, bytes + total, part, &written, nullptr) || written == 0) return false;
        total += written;
    }
    return true;
}

bool StartFfmpeg(const fs::path& outputFile, std::wstring& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = INVALID_HANDLE_VALUE;
    HANDLE writePipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        error = L"Не удалось создать канал FFmpeg: " + std::to_wstring(GetLastError());
        return false;
    }
    SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    int outputWidth = g_screenWidth;
    int outputHeight = g_screenHeight;
    if (outputWidth > g_config.videoMaxWidth) {
        outputWidth = g_config.videoMaxWidth;
        outputHeight = static_cast<int>((static_cast<std::int64_t>(g_screenHeight) * outputWidth) / g_screenWidth);
    }
    outputWidth = max(2, outputWidth - (outputWidth % 2));
    outputHeight = max(2, outputHeight - (outputHeight % 2));

    std::wstringstream command;
    command << L'"' << g_config.ffmpegPath << L"\" -y -hide_banner -loglevel error "
            << L"-f rawvideo -pix_fmt bgr0 -video_size " << g_screenWidth << L"x" << g_screenHeight << L" "
            << L"-framerate 1/" << g_config.captureIntervalSeconds
            << L" -i pipe:0 -an -vf scale=" << outputWidth << L":" << outputHeight << L":flags=area "
            << L"-fps_mode passthrough -c:v libx264 -preset " << g_config.videoPreset
            << L" -tune stillimage -crf " << g_config.videoCrf << L" "
            << L"-pix_fmt yuv420p -movflags +faststart \"" << outputFile.wstring() << L"\"";
    const std::wstring commandLine = command.str();
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = readPipe;
    startup.hStdOutput = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(g_config.ffmpegPath.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);

    CloseHandle(readPipe);
    if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);

    if (!started) {
        CloseHandle(writePipe);
        error = L"Не удалось запустить FFmpeg: " + std::to_wstring(GetLastError());
        return false;
    }

    g_ffmpegInput = writePipe;
    g_ffmpegProcess = process;
    return true;
}

void CaptureLoop(StudentSession student) {
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = g_screenWidth;
    info.bmiHeader.biHeight = -g_screenHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    if (!screen || !memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        SetCaptureError(L"Не удалось создать буфер снимка экрана.");
        if (g_ffmpegInput != INVALID_HANDLE_VALUE) {
            CloseHandle(g_ffmpegInput);
            g_ffmpegInput = INVALID_HANDLE_VALUE;
        }
        PostMessageW(g_mainWindow, kCaptureFailedMessage, 0, 0);
        return;
    }

    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    const int fontHeight = max(22, g_screenHeight / 55);
    HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memory, font);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(g_screenWidth) *
        static_cast<std::uint64_t>(g_screenHeight) * 4ULL;

    auto nextFrame = std::chrono::steady_clock::now();
    while (!g_stopCapture.load()) {
        if (!BitBlt(memory, 0, 0, g_screenWidth, g_screenHeight, screen,
            g_screenX, g_screenY, SRCCOPY | CAPTUREBLT)) {
            SetCaptureError(L"Windows не позволила получить снимок экрана.");
            PostMessageW(g_mainWindow, kCaptureFailedMessage, 0, 0);
            break;
        }

        const int boxHeight = fontHeight * 2 + 28;
        RECT box{12, 12, g_screenWidth - 12, 12 + boxHeight};
        HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memory, &box, background);
        DeleteObject(background);

        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, RGB(255, 255, 255));
        const std::wstring line1 = L"Группа: " + student.groupName + L"    Студент: " + student.fullName();
        const std::wstring line2 = L"Время: " + TimestampOverlay();
        RECT text1{26, 16, g_screenWidth - 24, 18 + fontHeight + 8};
        RECT text2{26, 20 + fontHeight, g_screenWidth - 24, 22 + fontHeight * 2 + 8};
        DrawTextW(memory, line1.c_str(), -1, &text1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        DrawTextW(memory, line2.c_str(), -1, &text2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        if (!WriteHandleAll(g_ffmpegInput, pixels, frameBytes)) {
            if (!g_stopCapture.load()) {
                SetCaptureError(L"FFmpeg прекратил принимать кадры.");
                PostMessageW(g_mainWindow, kCaptureFailedMessage, 0, 0);
            }
            break;
        }

        nextFrame += std::chrono::seconds(g_config.captureIntervalSeconds);
        std::unique_lock<std::mutex> waitLock(g_captureWakeMutex);
        g_captureWake.wait_until(waitLock, nextFrame, [] { return g_stopCapture.load(); });
    }

    SelectObject(memory, oldFont);
    SelectObject(memory, oldBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (g_ffmpegInput != INVALID_HANDLE_VALUE) {
        CloseHandle(g_ffmpegInput);
        g_ffmpegInput = INVALID_HANDLE_VALUE;
    }
}

bool WriteHttpData(HINTERNET request, const void* data, DWORD size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    DWORD total = 0;
    while (total < size) {
        DWORD written = 0;
        if (!WinHttpWriteData(request, bytes + total, size - total, &written) || written == 0) return false;
        total += written;
    }
    return true;
}

bool UploadVideo(const fs::path& file, const std::wstring& startedAt, const std::wstring& endedAt,
                 const std::wstring& recordingTitle, const StudentSession& student, std::wstring& error) {
    ParsedUrl parsed;
    if (!ParseUrl(JoinUrl(g_config.baseUrl, L"/api/upload.php"), parsed)) {
        error = L"Некорректный URL загрузки.";
        return false;
    }

    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) {
        error = L"Не удалось открыть созданное видео.";
        return false;
    }
    const std::streamoff signedSize = input.tellg();
    if (signedSize <= 0) {
        error = L"Созданное видео пусто.";
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(signedSize);
    input.seekg(0);

    const std::string boundary = "----ScanDisplayBoundary7MA4YWxkTrZu0gW";
    const auto field = [&](const char* name, const std::wstring& value) {
        return std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"" + name +
            "\"\r\n\r\n" + WideToUtf8(value) + "\r\n";
    };

    std::string prefix;
    prefix += field("title", recordingTitle);
    prefix += field("started_at", startedAt);
    prefix += field("ended_at", endedAt);
    prefix += field("computer_name", ComputerName());
    prefix += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"video\"; filename=\"recording.mp4\"\r\n";
    prefix += "Content-Type: video/mp4\r\n\r\n";
    const std::string suffix = "\r\n--" + boundary + "--\r\n";

    const std::uint64_t total64 = prefix.size() + fileSize + suffix.size();
    if (total64 > 0xFFFFFFFFULL) {
        error = L"Видео превышает лимит клиента 4 ГБ.";
        return false;
    }

    HINTERNET sessionHandle = WinHttpOpen(L"ScanDisplay/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sessionHandle) {
        error = L"WinHttpOpen: " + std::to_wstring(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(sessionHandle, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs);

    HINTERNET connection = WinHttpConnect(sessionHandle, parsed.host.c_str(), parsed.port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;

    bool success = false;
    if (!connection || !request) {
        error = L"Не удалось создать запрос загрузки: " + std::to_wstring(GetLastError());
    } else {
        const std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + Utf8ToWide(boundary) +
            L"\r\nAuthorization: Bearer " + student.token + L"\r\n";
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0,
            static_cast<DWORD>(total64), 0)) {
            error = L"Не удалось начать загрузку: " + std::to_wstring(GetLastError());
        } else if (!WriteHttpData(request, prefix.data(), static_cast<DWORD>(prefix.size()))) {
            error = L"Ошибка отправки заголовка файла.";
        } else {
            std::vector<char> buffer(1024 * 1024);
            bool writeOk = true;
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize got = input.gcount();
                if (got > 0 && !WriteHttpData(request, buffer.data(), static_cast<DWORD>(got))) {
                    writeOk = false;
                    break;
                }
            }

            if (!writeOk || !WriteHttpData(request, suffix.data(), static_cast<DWORD>(suffix.size()))) {
                error = L"Ошибка передачи видео на сервер.";
            } else if (!WinHttpReceiveResponse(request, nullptr)) {
                error = L"Сервер не вернул ответ: " + std::to_wstring(GetLastError());
            } else {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    nullptr, &status, &statusSize, nullptr);
                std::string responseBody;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                    const size_t offset = responseBody.size();
                    responseBody.resize(offset + available);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, responseBody.data() + offset, available, &read)) break;
                    responseBody.resize(offset + read);
                }

                if (status == 200 && responseBody.find("\"ok\":true") != std::string::npos) {
                    success = true;
                } else {
                    const std::string message = JsonString(responseBody, "message");
                    error = message.empty()
                        ? L"Сервер отклонил видео, HTTP " + std::to_wstring(status)
                        : Utf8ToWide(message);
                }
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(sessionHandle);
    return success;
}

std::wstring FormatFileSize(std::uintmax_t bytes) {
    if (bytes < 1024ULL) return std::to_wstring(bytes) + L" Б";

    double value = static_cast<double>(bytes);
    std::wstring unit = L"КБ";
    value /= 1024.0;
    if (value >= 1024.0) {
        value /= 1024.0;
        unit = L"МБ";
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        unit = L"ГБ";
    }

    std::wstringstream output;
    output << std::fixed << std::setprecision(value >= 100.0 ? 0 : 1)
           << value << L" " << unit;
    return output.str();
}

void UpdateTrayTooltip() {
    std::wstring tooltip = L"ScanDisplay — запись экрана";
    const AppState state = g_state.load();

    std::error_code fileError;
    const std::uintmax_t fileSize = !g_outputFile.empty() && fs::exists(g_outputFile, fileError)
        ? fs::file_size(g_outputFile, fileError)
        : 0;

    if (state == AppState::Recording) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_recordingStartedSteady).count();
        const long long minutes = elapsed / 60;
        const long long seconds = elapsed % 60;

        std::wstringstream text;
        text << L"Запись: " << minutes << L" мин "
             << std::setw(2) << std::setfill(L'0') << seconds
             << L" сек | " << FormatFileSize(fileSize);
        tooltip = text.str();
    } else if (state == AppState::Finalizing) {
        tooltip = L"Обработка видео | " + FormatFileSize(fileSize);
    }

    g_tray.uFlags = NIF_TIP;
    wcsncpy_s(g_tray.szTip, _countof(g_tray.szTip), tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void Notify(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO) {
    g_tray.uFlags = NIF_INFO;
    wcsncpy_s(g_tray.szInfoTitle, _countof(g_tray.szInfoTitle), title.c_str(), _TRUNCATE);
    wcsncpy_s(g_tray.szInfo, _countof(g_tray.szInfo), text.c_str(), _TRUNCATE);
    g_tray.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void FinalizeRecording(fs::path outputFile, std::wstring startedAt, std::wstring endedAt,
                       std::wstring recordingTitle, StudentSession student) {
    if (g_captureThread.joinable()) g_captureThread.join();

    DWORD exitCode = 1;
    if (g_ffmpegProcess.hProcess) {
        WaitForSingleObject(g_ffmpegProcess.hProcess, INFINITE);
        GetExitCodeProcess(g_ffmpegProcess.hProcess, &exitCode);
        CloseHandle(g_ffmpegProcess.hThread);
        CloseHandle(g_ffmpegProcess.hProcess);
        g_ffmpegProcess = {};
    }

    std::wstring error = GetCaptureError();
    bool ok = exitCode == 0 && fs::exists(outputFile);
    if (!ok && error.empty()) {
        error = L"FFmpeg завершился с кодом " + std::to_wstring(exitCode) + L".";
    }
    if (ok) ok = UploadVideo(outputFile, startedAt, endedAt, recordingTitle, student, error);

    if (ok && g_config.deleteAfterUpload) {
        std::error_code ec;
        fs::remove(outputFile, ec);
    }

    auto* message = new std::wstring(ok
        ? L"Видео успешно создано и отправлено на сервер."
        : L"Видео оставлено локально. Ошибка обработки или отправки: " + error);
    PostMessageW(g_mainWindow, kFinalizeMessage, ok ? 1 : 0, reinterpret_cast<LPARAM>(message));
}

void StartRecording(const std::wstring& recordingTitle) {
    if (g_state.load() != AppState::Idle) return;

    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    if (!student.authorized()) {
        MessageBoxW(g_mainWindow, L"Сначала выполните авторизацию по цифровому коду студента.",
            L"ScanDisplay", MB_ICONWARNING);
        return;
    }

    g_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_screenWidth <= 0 || g_screenHeight <= 0) {
        MessageBoxW(g_mainWindow, L"Не удалось определить размер экрана.", L"ScanDisplay", MB_ICONERROR);
        return;
    }

    const fs::path directory = g_config.outputDir / student.code / TimestampFolder();
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        MessageBoxW(g_mainWindow, L"Не удалось создать каталог записи.", L"ScanDisplay", MB_ICONERROR);
        return;
    }
    g_outputFile = directory / L"recording.mp4";

    std::wstring error;
    if (!StartFfmpeg(g_outputFile, error)) {
        MessageBoxW(g_mainWindow, error.c_str(), L"ScanDisplay", MB_ICONERROR);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_captureErrorMutex);
        g_captureError.clear();
    }
    g_startedAt = TimestampIso();
    g_recordingTitle = recordingTitle;
    g_recordingStartedSteady = std::chrono::steady_clock::now();
    g_recordingStudent = student;
    g_stopCapture.store(false);
    g_state.store(AppState::Recording);
    UpdateTrayTooltip();

    try {
        g_captureThread = std::thread(CaptureLoop, student);
    } catch (...) {
        g_stopCapture.store(true);
        if (g_ffmpegInput != INVALID_HANDLE_VALUE) {
            CloseHandle(g_ffmpegInput);
            g_ffmpegInput = INVALID_HANDLE_VALUE;
        }
        WaitForSingleObject(g_ffmpegProcess.hProcess, INFINITE);
        CloseHandle(g_ffmpegProcess.hThread);
        CloseHandle(g_ffmpegProcess.hProcess);
        g_ffmpegProcess = {};
        g_state.store(AppState::Idle);
        UpdateTrayTooltip();
        MessageBoxW(g_mainWindow, L"Не удалось запустить поток записи.", L"ScanDisplay", MB_ICONERROR);
        return;
    }

    Notify(L"ScanDisplay", L"Запись начата: " + recordingTitle + L". Интервал кадров: " +
        std::to_wstring(g_config.captureIntervalSeconds) + L" сек.");
}

void StopRecording() {
    AppState expected = AppState::Recording;
    if (!g_state.compare_exchange_strong(expected, AppState::Finalizing)) return;
    UpdateTrayTooltip();

    g_stopCapture.store(true);
    g_captureWake.notify_all();
    const std::wstring endedAt = TimestampIso();

    if (g_finalizeThread.joinable()) g_finalizeThread.join();
    g_finalizeThread = std::thread(FinalizeRecording, g_outputFile, g_startedAt, endedAt,
        g_recordingTitle, g_recordingStudent);
    Notify(L"ScanDisplay", L"Запись остановлена. MP4 завершается и отправляется на сервер.");
}


void ShowTrayMenu(HWND window) {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();

    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    const AppState state = g_state.load();
    const std::wstring authCaption = student.authorized()
        ? L"Авторизация студента: " + student.fullName()
        : L"Авторизация студента";

    AppendMenuW(menu, MF_STRING | (state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuAuth, authCaption.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuStart, L"Начать запись");
    AppendMenuW(menu, MF_STRING | (state == AppState::Recording ? MF_ENABLED : MF_GRAYED),
        kMenuStop, L"Остановить запись");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuExit, L"Выход");

    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
}

void OpenAuthWindow() {
    if (g_state.load() != AppState::Idle) return;
    if (g_authWindow && IsWindow(g_authWindow)) {
        ShowWindow(g_authWindow, SW_RESTORE);
        SetForegroundWindow(g_authWindow);
        return;
    }

    g_authWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, kAuthWindowClass,
        L"Авторизация ScanDisplay", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 230, g_mainWindow, nullptr, g_instance, nullptr);
    ShowWindow(g_authWindow, SW_SHOW);
    UpdateWindow(g_authWindow);
}

LRESULT CALLBACK AuthWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            CreateWindowW(L"STATIC", L"Введите цифровой код студента:", WS_CHILD | WS_VISIBLE,
                24, 22, 370, 22, window, nullptr, g_instance, nullptr);
            g_authEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 52, 370, 30, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthEditId)), g_instance, nullptr);
            CreateWindowW(L"BUTTON", L"Авторизоваться",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                24, 96, 180, 34, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthButtonId)), g_instance, nullptr);
            g_authStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                24, 142, 370, 42, window, nullptr, g_instance, nullptr);
            SendMessageW(g_authEdit, EM_SETLIMITTEXT, 32, 0);
            SetFocus(g_authEdit);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == kAuthButtonId) {
                wchar_t code[64]{};
                GetWindowTextW(g_authEdit, code, _countof(code));
                if (wcslen(code) < 4) {
                    SetWindowTextW(g_authStatus, L"Введите выданный студенту цифровой код.");
                    return 0;
                }

                EnableWindow(GetDlgItem(window, kAuthButtonId), FALSE);
                SetWindowTextW(g_authStatus, L"Проверка кода на сервере...");
                std::wstring error;
                if (Authenticate(code, error)) {
                    StudentSession student;
                    {
                        std::lock_guard<std::mutex> lock(g_studentMutex);
                        student = g_student;
                    }
                    Notify(L"Авторизация выполнена", student.groupName + L": " + student.fullName());
                    DestroyWindow(window);
                } else {
                    SetWindowTextW(g_authStatus, error.c_str());
                    EnableWindow(GetDlgItem(window, kAuthButtonId), TRUE);
                }
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            g_authWindow = nullptr;
            g_authEdit = nullptr;
            g_authStatus = nullptr;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenRecordingTitleWindow() {
    if (g_state.load() != AppState::Idle) return;

    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    if (!student.authorized()) {
        MessageBoxW(g_mainWindow, L"Сначала выполните авторизацию по цифровому коду студента.",
            L"ScanDisplay", MB_ICONWARNING);
        return;
    }

    if (g_recordingTitleWindow && IsWindow(g_recordingTitleWindow)) {
        ShowWindow(g_recordingTitleWindow, SW_RESTORE);
        SetForegroundWindow(g_recordingTitleWindow);
        return;
    }

    g_recordingTitleWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, kRecordingTitleWindowClass,
        L"Новая запись ScanDisplay", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 235, g_mainWindow, nullptr, g_instance, nullptr);
    ShowWindow(g_recordingTitleWindow, SW_SHOW);
    UpdateWindow(g_recordingTitleWindow);
}

LRESULT CALLBACK RecordingTitleWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            CreateWindowW(L"STATIC", L"Введите название работы или задания:", WS_CHILD | WS_VISIBLE,
                24, 22, 470, 22, window, nullptr, g_instance, nullptr);
            g_recordingTitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 52, 470, 30, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecordingTitleEditId)), g_instance, nullptr);
            CreateWindowW(L"BUTTON", L"Начать запись",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                24, 96, 190, 34, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecordingTitleButtonId)), g_instance, nullptr);
            g_recordingTitleStatus = CreateWindowW(L"STATIC", L"Название будет сохранено вместе с видео.",
                WS_CHILD | WS_VISIBLE, 24, 142, 470, 42, window, nullptr, g_instance, nullptr);
            SendMessageW(g_recordingTitleEdit, EM_SETLIMITTEXT, 255, 0);
            SetFocus(g_recordingTitleEdit);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == kRecordingTitleButtonId) {
                wchar_t buffer[256]{};
                GetWindowTextW(g_recordingTitleEdit, buffer, _countof(buffer));
                const std::wstring recordingTitle = TrimText(buffer);
                if (recordingTitle.empty()) {
                    SetWindowTextW(g_recordingTitleStatus, L"Введите название записи.");
                    SetFocus(g_recordingTitleEdit);
                    return 0;
                }
                DestroyWindow(window);
                StartRecording(recordingTitle);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            g_recordingTitleWindow = nullptr;
            g_recordingTitleEdit = nullptr;
            g_recordingTitleStatus = nullptr;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuAuth: OpenAuthWindow(); return 0;
                case kMenuStart: OpenRecordingTitleWindow(); return 0;
                case kMenuStop: StopRecording(); return 0;
                case kMenuExit:
                    if (g_state.load() == AppState::Idle) DestroyWindow(window);
                    return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == kTrayStatusTimerId) {
                UpdateTrayTooltip();
                return 0;
            }
            break;

        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU || event == WM_LBUTTONDBLCLK) {
                ShowTrayMenu(window);
                return 0;
            }
            break;
        }

        case kCaptureFailedMessage:
            if (g_state.load() == AppState::Recording) StopRecording();
            return 0;

        case kFinalizeMessage: {
            const bool ok = wParam != 0;
            auto* text = reinterpret_cast<std::wstring*>(lParam);
            if (g_finalizeThread.joinable()) g_finalizeThread.join();
            g_state.store(AppState::Idle);
            UpdateTrayTooltip();
            Notify(ok ? L"Запись отправлена" : L"Ошибка записи",
                text ? *text : L"Неизвестный результат.", ok ? NIIF_INFO : NIIF_ERROR);
            delete text;
            return 0;
        }

        case WM_DESTROY:
            KillTimer(window, kTrayStatusTimerId);
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterWindows() {
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.hInstance = g_instance;
    mainClass.lpfnWndProc = MainWindowProc;
    mainClass.lpszClassName = kMainWindowClass;
    mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    WNDCLASSEXW authClass = mainClass;
    authClass.lpfnWndProc = AuthWindowProc;
    authClass.lpszClassName = kAuthWindowClass;
    authClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    WNDCLASSEXW titleClass = mainClass;
    titleClass.lpfnWndProc = RecordingTitleWindowProc;
    titleClass.lpszClassName = kRecordingTitleWindowClass;
    titleClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0 &&
        RegisterClassExW(&titleClass) != 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    g_instance = instance;
    InitCommonControls();

    std::wstring configError;
    if (!LoadConfig(configError)) {
        MessageBoxW(nullptr, configError.c_str(), L"ScanDisplay", MB_ICONERROR);
        return 1;
    }
    if (g_configCreated) {
        MessageBoxW(nullptr,
            L"config.ini автоматически создан рядом с ScanDisplay.exe.\n"
            L"Используется адрес сервера http://127.0.0.1/scandisplay/server.\n"
            L"При необходимости измените адрес и перезапустите программу.",
            L"ScanDisplay", MB_ICONINFORMATION);
    }
    LoadSession();

    HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\ScanDisplayClientSingleton");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ScanDisplay уже запущен.", L"ScanDisplay", MB_ICONINFORMATION);
        if (singleton) CloseHandle(singleton);
        return 0;
    }

    if (!RegisterWindows()) {
        CloseHandle(singleton);
        return 1;
    }

    g_mainWindow = CreateWindowExW(0, kMainWindowClass, L"ScanDisplay", WS_OVERLAPPED,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_mainWindow) {
        CloseHandle(singleton);
        return 1;
    }

    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_mainWindow;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, _countof(g_tray.szTip), L"ScanDisplay — запись экрана");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    SetTimer(g_mainWindow, kTrayStatusTimerId, kTrayStatusIntervalMs, nullptr);
    UpdateTrayTooltip();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_stopCapture.store(true);
    g_captureWake.notify_all();
    if (g_captureThread.joinable()) g_captureThread.join();
    if (g_finalizeThread.joinable()) g_finalizeThread.join();
    if (g_ffmpegInput != INVALID_HANDLE_VALUE) CloseHandle(g_ffmpegInput);
    if (g_ffmpegProcess.hThread) CloseHandle(g_ffmpegProcess.hThread);
    if (g_ffmpegProcess.hProcess) CloseHandle(g_ffmpegProcess.hProcess);
    CloseHandle(singleton);
    return static_cast<int>(message.wParam);
}
