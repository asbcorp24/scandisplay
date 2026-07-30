from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"Marker not found: {label}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"Regex marker not found or ambiguous: {label} ({count})")
    return updated


main_path = Path("client/src/main.cpp")
source = main_path.read_text(encoding="utf-8")

source = replace_once(source, "#include <bcrypt.h>\n", "", "bcrypt include")
source = replace_once(source, '#pragma comment(lib, "bcrypt.lib")\n', "", "bcrypt pragma")
source = replace_once(
    source,
    'constexpr wchar_t kAdminWindowClass[] = L"ScanDisplayAdminWindow";\n',
    "",
    "admin window class",
)
source = replace_once(source, "constexpr UINT kMenuAdmin = 1000;\n", "", "admin menu id")
source = replace_once(
    source,
    "constexpr UINT kAdminLoginEditId = 2201;\n"
    "constexpr UINT kAdminPasswordEditId = 2202;\n"
    "constexpr UINT kAdminConfirmEditId = 2203;\n"
    "constexpr UINT kAdminButtonId = 2204;\n",
    "",
    "admin control ids",
)
source = replace_once(
    source,
    "HWND g_adminWindow = nullptr;\n"
    "HWND g_adminLoginEdit = nullptr;\n"
    "HWND g_adminPasswordEdit = nullptr;\n"
    "HWND g_adminConfirmEdit = nullptr;\n"
    "HWND g_adminStatus = nullptr;\n"
    "bool g_adminAuthenticated = false;\n"
    "bool g_adminSetupMode = false;\n"
    "std::wstring g_adminLogin;\n",
    "",
    "admin globals",
)
source = replace_once(
    source,
    "Config g_config;\n",
    "Config g_config;\nbool g_configCreated = false;\n",
    "config created flag",
)

create_default_config = r'''bool CreateDefaultConfig(const fs::path& configPath, std::wstring& error) {
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

'''
source = replace_once(
    source,
    "bool LoadConfig(std::wstring& error) {\n",
    create_default_config + "bool LoadConfig(std::wstring& error) {\n",
    "default config function",
)
source = replace_once(
    source,
    "bool LoadConfig(std::wstring& error) {\n"
    "    const fs::path configPath = fs::path(ModuleDirectory()) / L\"config.ini\";\n"
    "    if (!fs::exists(configPath)) {\n"
    "        error = L\"Рядом с программой отсутствует config.ini. Скопируйте config.example.ini и задайте адрес сервера и путь к FFmpeg.\";\n"
    "        return false;\n"
    "    }\n",
    "bool LoadConfig(std::wstring& error) {\n"
    "    const fs::path configPath = fs::path(ModuleDirectory()) / L\"config.ini\";\n"
    "\n"
    "    std::error_code obsoleteAdminError;\n"
    "    fs::remove(fs::path(ModuleDirectory()) / L\"admin.ini\", obsoleteAdminError);\n"
    "\n"
    "    if (!fs::exists(configPath)) {\n"
    "        if (!CreateDefaultConfig(configPath, error)) return false;\n"
    "        g_configCreated = true;\n"
    "    }\n",
    "automatic config creation",
)
source = replace_once(
    source,
    '        error = L"FFmpeg не найден: " + g_config.ffmpegPath;\n',
    '        error = (g_configCreated ? L"config.ini создан автоматически рядом с программой.\\n\\n" : L"") +\n'
    '            L"FFmpeg не найден: " + g_config.ffmpegPath +\n'
    '            L"\\nПоложите ffmpeg.exe рядом с ScanDisplay.exe либо измените recording.ffmpeg_path.";\n',
    "ffmpeg error after config creation",
)

source = regex_once(
    source,
    r"\nfs::path AdminFile\(\) \{.*?\nstd::string UrlEncode\(const std::string& value\) \{",
    "\nstd::string UrlEncode(const std::string& value) {",
    "admin credential helpers",
)

source = replace_once(
    source,
    "void StartRecording(const std::wstring& recordingTitle) {\n    if (!g_adminAuthenticated) return;\n",
    "void StartRecording(const std::wstring& recordingTitle) {\n",
    "start recording admin guard",
)
source = replace_once(
    source,
    "void StopRecording() {\n    if (!g_adminAuthenticated) return;\n",
    "void StopRecording() {\n",
    "stop recording admin guard",
)

tray_menu = r'''void ShowTrayMenu(HWND window) {
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
}'''
source = regex_once(
    source,
    r"\nvoid OpenAdminWindow\(\);.*?\nvoid ShowTrayMenu\(HWND window\) \{.*?\n\}\n\nvoid OpenAuthWindow\(\)",
    "\n" + tray_menu + "\n\nvoid OpenAuthWindow()",
    "admin window and protected tray menu",
)
source = replace_once(
    source,
    "void OpenAuthWindow() {\n    if (!RequireAdmin()) return;\n",
    "void OpenAuthWindow() {\n",
    "student auth admin guard",
)
source = replace_once(
    source,
    "void OpenRecordingTitleWindow() {\n    if (!RequireAdmin()) return;\n",
    "void OpenRecordingTitleWindow() {\n",
    "recording title admin guard",
)
source = replace_once(
    source,
    "                case kMenuAdmin: OpenAdminWindow(); return 0;\n",
    "",
    "admin menu command",
)
source = replace_once(
    source,
    "                    if (g_adminAuthenticated && g_state.load() == AppState::Idle) DestroyWindow(window);\n",
    "                    if (g_state.load() == AppState::Idle) DestroyWindow(window);\n",
    "exit admin guard",
)
source = replace_once(
    source,
    "    WNDCLASSEXW adminClass = mainClass;\n"
    "    adminClass.lpfnWndProc = AdminWindowProc;\n"
    "    adminClass.lpszClassName = kAdminWindowClass;\n"
    "    adminClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);\n"
    "\n",
    "",
    "admin window registration",
)
source = replace_once(
    source,
    "    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&adminClass) != 0 &&\n"
    "        RegisterClassExW(&authClass) != 0 && RegisterClassExW(&titleClass) != 0;\n",
    "    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0 &&\n"
    "        RegisterClassExW(&titleClass) != 0;\n",
    "window registration result",
)
source = replace_once(source, "\n    OpenAdminWindow();\n", "", "startup admin window")
source = replace_once(
    source,
    "    if (!LoadConfig(configError)) {\n"
    "        MessageBoxW(nullptr, configError.c_str(), L\"ScanDisplay\", MB_ICONERROR);\n"
    "        return 1;\n"
    "    }\n"
    "    LoadSession();\n",
    "    if (!LoadConfig(configError)) {\n"
    "        MessageBoxW(nullptr, configError.c_str(), L\"ScanDisplay\", MB_ICONERROR);\n"
    "        return 1;\n"
    "    }\n"
    "    if (g_configCreated) {\n"
    "        MessageBoxW(nullptr,\n"
    "            L\"config.ini автоматически создан рядом с ScanDisplay.exe.\\n\"\n"
    "            L\"Используется адрес сервера http://127.0.0.1/scandisplay/server.\\n\"\n"
    "            L\"При необходимости измените адрес и перезапустите программу.\",\n"
    "            L\"ScanDisplay\", MB_ICONINFORMATION);\n"
    "    }\n"
    "    LoadSession();\n",
    "first-start config notification",
)

for forbidden in (
    "g_admin",
    "AdminWindow",
    "kMenuAdmin",
    "AdminCredentialsConfigured",
    "bcrypt.h",
    "bcrypt.lib",
):
    if forbidden in source:
        raise SystemExit(f"Client administrator code remains: {forbidden}")

main_path.write_text(source, encoding="utf-8")

cmake_path = Path("client/CMakeLists.txt")
cmake = cmake_path.read_text(encoding="utf-8")
cmake = replace_once(cmake, "    bcrypt\n", "", "bcrypt CMake link")
cmake_path.write_text(cmake, encoding="utf-8")

readme_path = Path("README.md")
readme = readme_path.read_text(encoding="utf-8")
readme = readme.replace("copy client\\config.example.ini C:\\ScanDisplay\\config.ini\n", "", 1)

section9_start = readme.index("## 9. Создать config.ini")
section9_description = readme.index("### Описание параметров", section9_start)
section9 = '''## 9. Первый запуск и автоматическое создание config.ini

Копировать `config.ini` вручную больше не требуется. Если файла нет, при первом запуске `ScanDisplay.exe` автоматически создаёт его в той же папке:

```text
C:\\ScanDisplay\\config.ini
```

По умолчанию записываются настройки:

```ini
[server]
base_url=http://127.0.0.1/scandisplay/server

[recording]
ffmpeg_path=ffmpeg.exe
output_dir=%LOCALAPPDATA%\\ScanDisplay\\recordings
capture_interval_seconds=30
video_max_width=1280
video_crf=35
video_preset=veryslow
delete_after_upload=0

[client]
request_timeout_seconds=120
```

Программа сообщает о создании файла. Если сервер находится на другом компьютере, измените `base_url` на его адрес и перезапустите клиент. Папка с `ScanDisplay.exe` должна разрешать создание файлов.

'''
readme = readme[:section9_start] + section9 + readme[section9_description:]

section10_start = readme.index("## 10. Запустить программу")
section11_start = readme.index("## 11. Авторизовать студента", section10_start)
separator_start = readme.rfind("---", section10_start, section11_start)
if separator_start == -1:
    raise SystemExit("README section 10 separator not found")
section10 = '''## 10. Запустить программу

Запустите:

```text
C:\\ScanDisplay\\ScanDisplay.exe
```

Локального администратора в клиенте нет. Логин и пароль администратора используются только в серверной веб-панели.

При первом запуске клиент создаёт `config.ini`. После запуска значок программы находится в системном трее Windows рядом с часами. Если значок не виден, нажмите стрелку **Показать скрытые значки**.

Щёлкните по значку правой кнопкой мыши. Доступны пункты:

- **Авторизация студента**;
- **Начать запись**;
- **Остановить запись**;
- **Выход**.

Старый файл `admin.ini`, если он остался от предыдущей сборки, автоматически удаляется и больше не используется.

---

'''
readme = readme[:section10_start] + section10 + readme[section11_start:]
readme_path.write_text(readme, encoding="utf-8")

print("Client administrator removed; automatic config creation added.")
