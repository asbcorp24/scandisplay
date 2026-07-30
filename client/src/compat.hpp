#pragma once

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <commctrl.h>

#include <algorithm>
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
#include <system_error>
#include <thread>
#include <vector>

namespace scandisplay_compat {

namespace fs = std::filesystem;

inline fs::path ModuleDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

inline std::wstring ReadIniValue(const fs::path& file, const wchar_t* section,
                                 const wchar_t* key, const wchar_t* fallback) {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
        static_cast<DWORD>(buffer.size()), file.c_str());
    return buffer.data();
}

inline bool DownloadFfmpeg(const fs::path& target) {
    const fs::path configPath = ModuleDirectory() / L"config.ini";
    std::wstring baseUrl = ReadIniValue(configPath, L"server", L"base_url", L"");
    while (!baseUrl.empty() && baseUrl.back() == L'/') baseUrl.pop_back();
    if (baseUrl.empty()) return false;

    const std::wstring url = baseUrl + L"/api/ffmpeg.php";
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(L"ScanDisplay/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 600000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;

    fs::path temporary = target;
    temporary += L".download";
    HANDLE output = INVALID_HANDLE_VALUE;
    bool success = false;

    const auto cleanup = [&]() {
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (!success) {
            std::error_code removeError;
            fs::remove(temporary, removeError);
        }
    };

    if (!connection || !request ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        cleanup();
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    if (status != 200) {
        cleanup();
        return false;
    }

    std::error_code directoryError;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), directoryError);
    if (directoryError) {
        cleanup();
        return false;
    }

    std::error_code removeError;
    fs::remove(temporary, removeError);
    output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        cleanup();
        return false;
    }

    std::vector<std::uint8_t> buffer(1024 * 1024);
    std::uint64_t total = 0;
    bool complete = false;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) {
            complete = true;
            break;
        }

        const DWORD toRead = available < static_cast<DWORD>(buffer.size())
            ? available : static_cast<DWORD>(buffer.size());
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data(), toRead, &received) || received == 0) break;

        DWORD offset = 0;
        while (offset < received) {
            DWORD written = 0;
            if (!WriteFile(output, buffer.data() + offset, received - offset, &written, nullptr) || written == 0) {
                complete = false;
                offset = received;
                break;
            }
            offset += written;
        }
        if (offset != received) break;
        total += received;
    }

    FlushFileBuffers(output);
    CloseHandle(output);
    output = INVALID_HANDLE_VALUE;

    if (!complete || total < 1024ULL * 1024ULL) {
        cleanup();
        return false;
    }

    std::ifstream check(temporary, std::ios::binary);
    char signature[2]{};
    check.read(signature, 2);
    const bool validExe = check.gcount() == 2 && signature[0] == 'M' && signature[1] == 'Z';
    check.close();
    if (!validExe) {
        cleanup();
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        cleanup();
        return false;
    }

    success = true;
    cleanup();
    return true;
}

inline bool ExistsWithFfmpegDownload(const fs::path& path) {
    std::error_code error;
    if (fs::exists(path, error)) return true;
    if (_wcsicmp(path.filename().c_str(), L"ffmpeg.exe") != 0) return false;
    return DownloadFfmpeg(path);
}

} // namespace scandisplay_compat

namespace std::filesystem {
inline bool scandisplay_exists(const path& value) {
    return ::scandisplay_compat::ExistsWithFfmpegDownload(value);
}

inline bool scandisplay_exists(const path& value, std::error_code& error) noexcept {
    return std::filesystem::exists(value, error);
}
} // namespace std::filesystem

#define exists scandisplay_exists

using std::max;
using std::min;
