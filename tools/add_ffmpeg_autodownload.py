from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "client" / "src" / "main.cpp"
README = ROOT / "README.md"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


text = MAIN.read_text(encoding="utf-8")

text = replace_once(
    text,
    "Config g_config;\nbool g_configCreated = false;",
    "Config g_config;\nbool g_configCreated = false;\nbool g_ffmpegDownloaded = false;",
    "ffmpeg downloaded flag",
)

text = replace_once(
    text,
    "std::wstring ReadIni(const fs::path& file, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {\n"
    "    std::vector<wchar_t> buffer(32768);\n"
    "    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());\n"
    "    return buffer.data();\n"
    "}\n\n",
    "std::wstring ReadIni(const fs::path& file, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {\n"
    "    std::vector<wchar_t> buffer(32768);\n"
    "    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());\n"
    "    return buffer.data();\n"
    "}\n\n"
    "bool DownloadServerFfmpeg(const fs::path& target, std::wstring& error);\n\n",
    "download forward declaration",
)

text = replace_once(
    text,
    "    if (!fs::exists(g_config.ffmpegPath)) {\n"
    "        error = (g_configCreated ? L\"config.ini создан автоматически рядом с программой.\\n\\n\" : L\"\") +\n"
    "            L\"FFmpeg не найден: \" + g_config.ffmpegPath +\n"
    "            L\"\\nПоложите ffmpeg.exe рядом с ScanDisplay.exe либо измените recording.ffmpeg_path.\";\n"
    "        return false;\n"
    "    }\n",
    "    if (!fs::exists(g_config.ffmpegPath)) {\n"
    "        std::wstring downloadError;\n"
    "        if (!DownloadServerFfmpeg(g_config.ffmpegPath, downloadError)) {\n"
    "            error = (g_configCreated ? L\"config.ini создан автоматически рядом с программой.\\n\\n\" : L\"\") +\n"
    "                L\"FFmpeg не найден: \" + g_config.ffmpegPath +\n"
    "                L\"\\nКлиент попытался скачать его с сервера, но загрузка не удалась.\\n\" + downloadError;\n"
    "            return false;\n"
    "        }\n"
    "        g_ffmpegDownloaded = true;\n"
    "    }\n",
    "missing ffmpeg handling",
)

function_code = r'''bool DownloadServerFfmpeg(const fs::path& target, std::wstring& error) {
    ParsedUrl parsed;
    if (!ParseUrl(JoinUrl(g_config.baseUrl, L"/api/ffmpeg.php"), parsed)) {
        error = L"Некорректный URL загрузки FFmpeg.";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"ScanDisplay/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = L"Не удалось открыть соединение для загрузки FFmpeg: " + std::to_wstring(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 600000);

    HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;

    fs::path temporary = target;
    temporary += L".download";
    HANDLE output = INVALID_HANDLE_VALUE;
    bool success = false;

    const auto finish = [&]() {
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (!success) {
            std::error_code removeError;
            fs::remove(temporary, removeError);
        }
    };

    if (!connection || !request) {
        error = L"Не удалось создать запрос загрузки FFmpeg: " + std::to_wstring(GetLastError());
        finish();
        return false;
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        error = L"Ошибка связи с сервером при загрузке FFmpeg: " + std::to_wstring(GetLastError());
        finish();
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    if (status != 200) {
        error = L"Сервер не выдал ffmpeg.exe, HTTP " + std::to_wstring(status) +
            L". Разместите файл в server/storage/ffmpeg/ffmpeg.exe.";
        finish();
        return false;
    }

    std::error_code directoryError;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), directoryError);
    if (directoryError) {
        error = L"Не удалось создать папку для FFmpeg: " + target.parent_path().wstring();
        finish();
        return false;
    }

    std::error_code removeOldError;
    fs::remove(temporary, removeOldError);
    output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = L"Не удалось создать временный файл FFmpeg: " + std::to_wstring(GetLastError());
        finish();
        return false;
    }

    std::uint64_t total = 0;
    bool downloadComplete = false;
    std::vector<std::uint8_t> buffer(1024 * 1024);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = L"Ошибка чтения FFmpeg с сервера: " + std::to_wstring(GetLastError());
            break;
        }
        if (available == 0) {
            downloadComplete = true;
            break;
        }

        const DWORD toRead = min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data(), toRead, &received) || received == 0) {
            error = L"Сервер прервал загрузку FFmpeg.";
            break;
        }

        DWORD offset = 0;
        while (offset < received) {
            DWORD written = 0;
            if (!WriteFile(output, buffer.data() + offset, received - offset, &written, nullptr) || written == 0) {
                error = L"Не удалось сохранить FFmpeg: " + std::to_wstring(GetLastError());
                downloadComplete = false;
                offset = received;
                break;
            }
            offset += written;
        }
        if (!error.empty()) break;
        total += received;
    }

    FlushFileBuffers(output);
    CloseHandle(output);
    output = INVALID_HANDLE_VALUE;

    if (!downloadComplete || total < 1024ULL * 1024ULL) {
        if (error.empty()) error = L"Сервер передал пустой или слишком маленький ffmpeg.exe.";
        finish();
        return false;
    }

    std::ifstream check(temporary, std::ios::binary);
    char signature[2]{};
    check.read(signature, 2);
    if (check.gcount() != 2 || signature[0] != 'M' || signature[1] != 'Z') {
        error = L"Сервер передал файл, который не является Windows ffmpeg.exe.";
        finish();
        return false;
    }
    check.close();

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"Не удалось установить скачанный ffmpeg.exe: " + std::to_wstring(GetLastError());
        finish();
        return false;
    }

    success = true;
    finish();
    return true;
}

'''

text = replace_once(
    text,
    "struct HttpResponse {\n",
    function_code + "struct HttpResponse {\n",
    "download function insertion",
)

text = replace_once(
    text,
    "    if (g_configCreated) {\n"
    "        MessageBoxW(nullptr,\n"
    "            L\"config.ini автоматически создан рядом с ScanDisplay.exe.\\n\"\n"
    "            L\"Используется адрес сервера http://127.0.0.1/scandisplay/server.\\n\"\n"
    "            L\"При необходимости измените адрес и перезапустите программу.\",\n"
    "            L\"ScanDisplay\", MB_ICONINFORMATION);\n"
    "    }\n"
    "    LoadSession();\n",
    "    if (g_configCreated) {\n"
    "        MessageBoxW(nullptr,\n"
    "            L\"config.ini автоматически создан рядом с ScanDisplay.exe.\\n\"\n"
    "            L\"Используется адрес сервера http://127.0.0.1/scandisplay/server.\\n\"\n"
    "            L\"При необходимости измените адрес и перезапустите программу.\",\n"
    "            L\"ScanDisplay\", MB_ICONINFORMATION);\n"
    "    }\n"
    "    if (g_ffmpegDownloaded) {\n"
    "        MessageBoxW(nullptr,\n"
    "            L\"ffmpeg.exe не был найден и автоматически скачан с сервера.\",\n"
    "            L\"ScanDisplay\", MB_ICONINFORMATION);\n"
    "    }\n"
    "    LoadSession();\n",
    "download success message",
)

MAIN.write_text(text, encoding="utf-8", newline="\n")

readme = README.read_text(encoding="utf-8")
if "## Автоматическая загрузка FFmpeg с сервера" not in readme:
    readme += r'''

---

## Автоматическая загрузка FFmpeg с сервера

Если рядом с `ScanDisplay.exe` отсутствует `ffmpeg.exe`, клиент автоматически выполняет запрос:

```text
<server.base_url>/api/ffmpeg.php
```

На сервере необходимо один раз разместить Windows-сборку FFmpeg по пути:

```text
server/storage/ffmpeg/ffmpeg.exe
```

Например для XAMPP:

```powershell
mkdir C:\xampp\htdocs\scandisplay\server\storage\ffmpeg
copy C:\ffmpeg\bin\ffmpeg.exe C:\xampp\htdocs\scandisplay\server\storage\ffmpeg\ffmpeg.exe
```

Каталог закрыт от прямого просмотра через Apache. Клиент скачивает файл через контролируемый PHP-обработчик, сохраняет его сначала как `ffmpeg.exe.download`, проверяет минимальный размер и сигнатуру Windows EXE `MZ`, затем переименовывает в `ffmpeg.exe`.

Автоматическую выдачу можно отключить переменной окружения:

```text
SCANDISPLAY_FFMPEG_DOWNLOAD=0
```

Другой путь к серверному файлу можно задать переменной:

```text
SCANDISPLAY_FFMPEG_FILE=C:\path\to\ffmpeg.exe
```
'''
README.write_text(readme, encoding="utf-8", newline="\n")
