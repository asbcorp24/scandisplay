#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <commctrl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kMainWindowClass[] = L"ScanDisplayMainWindow";
constexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kFinalizeMessage = WM_APP + 2;
constexpr UINT kTrayId = 1;

constexpr UINT kMenuAuth = 1001;
constexpr UINT kMenuStart = 1002;
constexpr UINT kMenuStop = 1003;
constexpr UINT kMenuExit = 1004;
constexpr UINT kAuthEditId = 2001;
constexpr UINT kAuthButtonId = 2002;

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HWND g_authWindow = nullptr;
HWND g_authEdit = nullptr;
HWND g_authStatus = nullptr;
NOTIFYICONDATAW g_tray{};
ULONG_PTR g_gdiplusToken = 0;

struct Config {
    std::wstring baseUrl;
    std::wstring ffmpegPath;
    fs::path outputDir;
    bool keepFrames = false;
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
StudentSession g_student;
std::mutex g_studentMutex;
std::atomic<AppState> g_state{AppState::Idle};
std::atomic_bool g_stopCapture{false};
std::thread g_captureThread;
std::thread g_finalizeThread;
fs::path g_sessionDir;
std::wstring g_startedAt;

std::wstring ModuleDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return fs::path(std::wstring(buffer.data(), length)).parent_path().wstring();
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

bool LoadConfig(std::wstring& error) {
    const fs::path configPath = fs::path(ModuleDirectory()) / L"config.ini";
    if (!fs::exists(configPath)) {
        error = L"Рядом с программой отсутствует config.ini. Скопируйте config.example.ini и задайте адрес сервера и путь к FFmpeg.";
        return false;
    }

    g_config.baseUrl = ReadIni(configPath, L"server", L"base_url", L"");
    g_config.ffmpegPath = ExpandEnvironment(ReadIni(configPath, L"recording", L"ffmpeg_path", L"ffmpeg.exe"));
    g_config.outputDir = ExpandEnvironment(ReadIni(configPath, L"recording", L"output_dir", L"%LOCALAPPDATA%\\ScanDisplay\\recordings"));
    g_config.keepFrames = ReadIni(configPath, L"recording", L"keep_frames", L"0") == L"1";

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

void SaveSession() {
    std::lock_guard<std::mutex> lock(g_studentMutex);
    const fs::path file = SessionFile();
    WritePrivateProfileStringW(L"student", L"id", std::to_wstring(g_student.id).c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"code", g_student.code.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"first_name", g_student.firstName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"last_name", g_student.lastName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"group_name", g_student.groupName.c_str(), file.c_str());
    WritePrivateProfileStringW(L"student", L"token", g_student.token.c_str(), file.c_str());
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
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string UrlEncode(const std::string& value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            output << static_cast<char>(c);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return output.str();
}

std::wstring JoinUrl(const std::wstring& base, const wchar_t* endpoint) {
    if (!endpoint || !*endpoint) return base;
    if (endpoint[0] == L'/') return base + endpoint;
    return base + L"/" + endpoint;
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

    HINTERNET session = WinHttpOpen(L"ScanDisplay/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = L"WinHttpOpen: " + std::to_wstring(GetLastError());
        return response;
    }
    WinHttpSetTimeouts(session, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs, g_config.timeoutMs);

    HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;

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
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &response.status, &size, nullptr);
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
    const std::string form = "code=" + UrlEncode(WideToUtf8(code)) + "&computer_name=" + UrlEncode(WideToUtf8(ComputerName()));
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
        g_student = std::move(session);
    }
    SaveSession();
    return true;
}

std::wstring LocalTimestamp(const wchar_t* format) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[128]{};
    swprintf_s(buffer, format, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

bool SaveBitmap32(HDC dc, HBITMAP bitmap, int width, int height, const fs::path& file) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (pixelBytes > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    std::vector<std::uint8_t> pixels(static_cast<size_t>(pixelBytes));
    if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS) == 0) return false;

    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    header.bfSize = static_cast<DWORD>(header.bfOffBits + pixels.size());

    std::ofstream output(file, std::ios::binary);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return output.good();
}

bool CaptureScreen(const fs::path& file, const StudentSession& student, std::wstring& error) {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        error = L"Не удалось определить размер экрана.";
        return false;
    }

    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    if (!screen || !memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        error = L"Не удалось создать буфер снимка экрана.";
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT);
    if (copied) {
        const int boxHeight = 78;
        RECT box{12, 12, width - 12, 12 + boxHeight};
        HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memory, &box, background);
        DeleteObject(background);

        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, RGB(255, 255, 255));
        HFONT font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(memory, font);

        const std::wstring line1 = L"Группа: " + student.groupName + L"    Студент: " + student.fullName();
        const std::wstring line2 = L"Время: " + LocalTimestamp(L"%02u.%02u.%04u %02u:%02u:%02u");
        RECT text1{26, 18, width - 24, 48};
        RECT text2{26, 48, width - 24, 76};
        DrawTextW(memory, line1.c_str(), -1, &text1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        DrawTextW(memory, line2.c_str(), -1, &text2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SelectObject(memory, oldFont);
        DeleteObject(font);
    }

    const bool saved = copied && SaveBitmap32(memory, bitmap, width, height, file);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (!saved) error = L"Не удалось сохранить снимок экрана.";
    return saved;
}

void CaptureLoop(fs::path directory, StudentSession student) {
    std::uint64_t frame = 1;
    while (!g_stopCapture.load()) {
        std::wstringstream name;
        name << L"frame_" << std::setw(6) << std::setfill(L'0') << frame << L".bmp";
        std::wstring error;
        CaptureScreen(directory / name.str(), student, error);
        ++frame;

        for (int i = 0; i < 10 && !g_stopCapture.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool RunHiddenProcess(const std::wstring& commandLine, DWORD& exitCode, std::wstring& error) {
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = L"Не удалось запустить FFmpeg: " + std::to_wstring(GetLastError());
        return false;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exitCode != 0) {
        error = L"FFmpeg завершился с кодом " + std::to_wstring(exitCode) + L".";
        return false;
    }
    return true;
}

bool EncodeVideo(const fs::path& directory, fs::path& outputFile, std::wstring& error) {
    if (!fs::exists(g_config.ffmpegPath)) {
        error = L"FFmpeg не найден: " + g_config.ffmpegPath;
        return false;
    }

    outputFile = directory / L"recording.mp4";
    const fs::path pattern = directory / L"frame_%06d.bmp";
    std::wstringstream command;
    command << L'"' << g_config.ffmpegPath << L"\" -y -hide_banner -loglevel error "
            << L"-framerate 1 -i \"" << pattern.wstring() << L"\" "
            << L"-c:v libx264 -preset veryfast -crf 23 -r 25 -pix_fmt yuv420p -movflags +faststart \""
            << outputFile.wstring() << L"\"";

    DWORD exitCode = 0;
    return RunHiddenProcess(command.str(), exitCode, error) && fs::exists(outputFile);
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
                 const StudentSession& student, std::wstring& error) {
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
    const std::streamoff fileSizeSigned = input.tellg();
    if (fileSizeSigned < 0) {
        error = L"Не удалось определить размер видео.";
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(fileSizeSigned);
    input.seekg(0);

    const std::string boundary = "----ScanDisplayBoundary7MA4YWxkTrZu0gW";
    const auto field = [&](const char* name, const std::wstring& value) {
        return std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" +
            WideToUtf8(value) + "\r\n";
    };
    std::string prefix;
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
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
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
                    error = message.empty() ? L"Сервер отклонил видео, HTTP " + std::to_wstring(status) : Utf8ToWide(message);
                }
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(sessionHandle);
    return success;
}

void RemoveFrames(const fs::path& directory) {
    if (g_config.keepFrames) return;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.path().extension() == L".bmp") fs::remove(entry.path(), ec);
    }
}

void Notify(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO) {
    g_tray.uFlags = NIF_INFO;
    wcsncpy_s(g_tray.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_tray.szInfo, text.c_str(), _TRUNCATE);
    g_tray.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void FinalizeRecording(fs::path directory, std::wstring startedAt, std::wstring endedAt, StudentSession student) {
    fs::path video;
    std::wstring error;
    bool ok = EncodeVideo(directory, video, error);
    if (ok) ok = UploadVideo(video, startedAt, endedAt, student, error);
    if (ok) RemoveFrames(directory);

    auto* message = new std::wstring(ok
        ? L"Видео успешно создано и отправлено на сервер."
        : L"Видео сохранено локально, но обработка или отправка завершилась ошибкой: " + error);
    PostMessageW(g_mainWindow, kFinalizeMessage, ok ? 1 : 0, reinterpret_cast<LPARAM>(message));
}

void StartRecording() {
    if (g_state.load() != AppState::Idle) return;

    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    if (!student.authorized()) {
        MessageBoxW(g_mainWindow, L"Сначала выполните авторизацию по цифровому коду студента.", L"ScanDisplay", MB_ICONWARNING);
        return;
    }
    if (!fs::exists(g_config.ffmpegPath)) {
        MessageBoxW(g_mainWindow, (L"Не найден FFmpeg:\n" + g_config.ffmpegPath).c_str(), L"ScanDisplay", MB_ICONERROR);
        return;
    }

    const std::wstring folder = LocalTimestamp(L"%04u%02u%02u_%02u%02u%02u");
    g_sessionDir = g_config.outputDir / student.code / folder;
    std::error_code ec;
    fs::create_directories(g_sessionDir, ec);
    if (ec) {
        MessageBoxW(g_mainWindow, L"Не удалось создать каталог записи.", L"ScanDisplay", MB_ICONERROR);
        return;
    }

    g_startedAt = LocalTimestamp(L"%04u-%02u-%02u %02u:%02u:%02u");
    g_stopCapture.store(false);
    g_state.store(AppState::Recording);
    g_captureThread = std::thread(CaptureLoop, g_sessionDir, student);
    Notify(L"ScanDisplay", L"Запись экрана начата.");
}

void StopRecording() {
    if (g_state.load() != AppState::Recording) return;
    g_state.store(AppState::Finalizing);
    g_stopCapture.store(true);
    if (g_captureThread.joinable()) g_captureThread.join();

    const std::wstring endedAt = LocalTimestamp(L"%04u-%02u-%02u %02u:%02u:%02u");
    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    if (g_finalizeThread.joinable()) g_finalizeThread.join();
    g_finalizeThread = std::thread(FinalizeRecording, g_sessionDir, g_startedAt, endedAt, student);
    Notify(L"ScanDisplay", L"Запись остановлена. Создаётся MP4 и выполняется отправка.");
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
    const std::wstring authCaption = student.authorized() ? L"Авторизация: " + student.fullName() : L"Авторизация";
    AppendMenuW(menu, MF_STRING, kMenuAuth, authCaption.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    const AppState state = g_state.load();
    AppendMenuW(menu, MF_STRING | (state == AppState::Idle ? MF_ENABLED : MF_GRAYED), kMenuStart, L"Начать запись");
    AppendMenuW(menu, MF_STRING | (state == AppState::Recording ? MF_ENABLED : MF_GRAYED), kMenuStop, L"Остановить запись");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state == AppState::Idle ? MF_ENABLED : MF_GRAYED), kMenuExit, L"Выход");

    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
}

void OpenAuthWindow() {
    if (g_authWindow && IsWindow(g_authWindow)) {
        ShowWindow(g_authWindow, SW_RESTORE);
        SetForegroundWindow(g_authWindow);
        return;
    }
    g_authWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, kAuthWindowClass, L"Авторизация ScanDisplay",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 440, 230,
        g_mainWindow, nullptr, g_instance, nullptr);
    ShowWindow(g_authWindow, SW_SHOW);
    UpdateWindow(g_authWindow);
}

LRESULT CALLBACK AuthWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", L"Введите цифровой код студента:", WS_CHILD | WS_VISIBLE,
                24, 22, 370, 22, window, nullptr, g_instance, nullptr);
            g_authEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 52, 370, 30, window, reinterpret_cast<HMENU>(kAuthEditId), g_instance, nullptr);
            CreateWindowW(L"BUTTON", L"Авторизоваться", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                24, 96, 180, 34, window, reinterpret_cast<HMENU>(kAuthButtonId), g_instance, nullptr);
            g_authStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                24, 142, 370, 42, window, nullptr, g_instance, nullptr);
            SendMessageW(g_authEdit, EM_SETLIMITTEXT, 32, 0);
            SetFocus(g_authEdit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kAuthButtonId) {
                wchar_t code[64]{};
                GetWindowTextW(g_authEdit, code, static_cast<int>(std::size(code)));
                if (wcslen(code) < 4) {
                    SetWindowTextW(g_authStatus, L"Введите выданный студенту цифровой код.");
                    return 0;
                }
                EnableWindow(GetDlgItem(window, kAuthButtonId), FALSE);
                SetWindowTextW(g_authStatus, L"Проверка кода на сервере...");
                std::wstring error;
                if (Authenticate(code, error)) {
                    StudentSession student;
                    { std::lock_guard<std::mutex> lock(g_studentMutex); student = g_student; }
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

LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuAuth: OpenAuthWindow(); return 0;
                case kMenuStart: StartRecording(); return 0;
                case kMenuStop: StopRecording(); return 0;
                case kMenuExit: DestroyWindow(window); return 0;
            }
            break;
        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU || lParam == WM_LBUTTONDBLCLK) {
                ShowTrayMenu(window);
                return 0;
            }
            break;
        case kFinalizeMessage: {
            const bool ok = wParam != 0;
            auto* text = reinterpret_cast<std::wstring*>(lParam);
            if (g_finalizeThread.joinable()) g_finalizeThread.join();
            g_state.store(AppState::Idle);
            Notify(ok ? L"Запись отправлена" : L"Ошибка записи", text ? *text : L"Неизвестный результат.", ok ? NIIF_INFO : NIIF_ERROR);
            delete text;
            return 0;
        }
        case WM_DESTROY:
            if (g_state.load() != AppState::Idle) {
                MessageBoxW(window, L"Сначала завершите запись и дождитесь отправки видео.", L"ScanDisplay", MB_ICONWARNING);
                return 0;
            }
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
    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    g_instance = instance;
    InitCommonControls();

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    std::wstring configError;
    if (!LoadConfig(configError)) {
        MessageBoxW(nullptr, configError.c_str(), L"ScanDisplay", MB_ICONERROR);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    LoadSession();

    HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\ScanDisplayClientSingleton");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ScanDisplay уже запущен.", L"ScanDisplay", MB_ICONINFORMATION);
        if (singleton) CloseHandle(singleton);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    if (!RegisterWindows()) {
        CloseHandle(singleton);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    g_mainWindow = CreateWindowExW(0, kMainWindowClass, L"ScanDisplay", WS_OVERLAPPED,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_mainWindow) {
        CloseHandle(singleton);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_mainWindow;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"ScanDisplay — запись экрана");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_captureThread.joinable()) {
        g_stopCapture.store(true);
        g_captureThread.join();
    }
    if (g_finalizeThread.joinable()) g_finalizeThread.join();
    CloseHandle(singleton);
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
