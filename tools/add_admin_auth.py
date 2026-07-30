from pathlib import Path

main = Path("client/src/main.cpp")
source = main.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str) -> None:
    global source
    if old not in source:
        raise SystemExit(f"Marker not found: {label}")
    source = source.replace(old, new, 1)


replace_once(
    "#include <commctrl.h>\n",
    "#include <commctrl.h>\n#include <bcrypt.h>\n",
    "bcrypt include",
)
replace_once(
    '#pragma comment(lib, "comctl32.lib")\n',
    '#pragma comment(lib, "comctl32.lib")\n#pragma comment(lib, "bcrypt.lib")\n',
    "bcrypt pragma",
)
replace_once(
    'constexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";\n',
    'constexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";\nconstexpr wchar_t kAdminWindowClass[] = L"ScanDisplayAdminWindow";\n',
    "admin window class",
)
replace_once(
    "constexpr UINT kMenuAuth = 1001;\n",
    "constexpr UINT kMenuAdmin = 1000;\nconstexpr UINT kMenuAuth = 1001;\n",
    "admin menu id",
)
replace_once(
    "constexpr UINT kRecordingTitleButtonId = 2102;\n",
    """constexpr UINT kRecordingTitleButtonId = 2102;
constexpr UINT kAdminLoginEditId = 2201;
constexpr UINT kAdminPasswordEditId = 2202;
constexpr UINT kAdminConfirmEditId = 2203;
constexpr UINT kAdminButtonId = 2204;
""",
    "admin control ids",
)
replace_once(
    "HWND g_authStatus = nullptr;\n",
    """HWND g_authStatus = nullptr;
HWND g_adminWindow = nullptr;
HWND g_adminLoginEdit = nullptr;
HWND g_adminPasswordEdit = nullptr;
HWND g_adminConfirmEdit = nullptr;
HWND g_adminStatus = nullptr;
bool g_adminAuthenticated = false;
bool g_adminSetupMode = false;
std::wstring g_adminLogin;
""",
    "admin globals",
)

admin_helpers = r'''
fs::path AdminFile() {
    return fs::path(ModuleDirectory()) / L"admin.ini";
}

std::wstring BytesToHex(const std::vector<std::uint8_t>& bytes) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(bytes.size() * 2);
    for (const std::uint8_t value : bytes) {
        result.push_back(digits[(value >> 4) & 0x0F]);
        result.push_back(digits[value & 0x0F]);
    }
    return result;
}

bool HexToBytes(const std::wstring& text, std::vector<std::uint8_t>& bytes) {
    if (text.empty() || text.size() % 2 != 0) return false;
    auto valueOf = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };

    bytes.clear();
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = valueOf(text[i]);
        const int low = valueOf(text[i + 1]);
        if (high < 0 || low < 0) {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool DeriveAdminPasswordHash(const std::wstring& password,
                             const std::vector<std::uint8_t>& salt,
                             std::vector<std::uint8_t>& hash) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0) return false;

    hash.assign(32, 0);
    auto* passwordBytes = reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(password.data()));
    const ULONG passwordSize = static_cast<ULONG>(password.size() * sizeof(wchar_t));
    status = BCryptDeriveKeyPBKDF2(
        algorithm,
        passwordBytes,
        passwordSize,
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        150000,
        hash.data(),
        static_cast<ULONG>(hash.size()),
        0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
}

bool AdminCredentialsConfigured() {
    const fs::path file = AdminFile();
    if (!fs::exists(file)) return false;

    const std::wstring login = TrimText(ReadIni(file, L"admin", L"login", L""));
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> hash;
    return !login.empty() &&
        HexToBytes(ReadIni(file, L"admin", L"salt", L""), salt) && salt.size() == 16 &&
        HexToBytes(ReadIni(file, L"admin", L"password_hash", L""), hash) && hash.size() == 32;
}

bool SaveAdminCredentials(const std::wstring& login, const std::wstring& password,
                          std::wstring& error) {
    if (login.size() < 3 || login.size() > 64 ||
        login.find_first_of(L"\r\n[]=;") != std::wstring::npos) {
        error = L"Логин должен содержать от 3 до 64 символов без [ ] = ;.";
        return false;
    }
    if (password.size() < 8 || password.size() > 128) {
        error = L"Пароль должен содержать от 8 до 128 символов.";
        return false;
    }

    std::vector<std::uint8_t> salt(16);
    if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        error = L"Не удалось создать криптографическую соль.";
        return false;
    }

    std::vector<std::uint8_t> hash;
    if (!DeriveAdminPasswordHash(password, salt, hash)) {
        error = L"Не удалось сформировать защищённый хэш пароля.";
        return false;
    }

    const fs::path file = AdminFile();
    const std::wstring saltHex = BytesToHex(salt);
    const std::wstring hashHex = BytesToHex(hash);
    if (!WritePrivateProfileStringW(L"admin", L"login", login.c_str(), file.c_str()) ||
        !WritePrivateProfileStringW(L"admin", L"salt", saltHex.c_str(), file.c_str()) ||
        !WritePrivateProfileStringW(L"admin", L"password_hash", hashHex.c_str(), file.c_str())) {
        error = L"Не удалось записать admin.ini рядом с ScanDisplay.exe. Проверьте права на папку.";
        return false;
    }
    return true;
}

bool VerifyAdminCredentials(const std::wstring& login, const std::wstring& password,
                            std::wstring& error) {
    const fs::path file = AdminFile();
    const std::wstring storedLogin = TrimText(ReadIni(file, L"admin", L"login", L""));
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> storedHash;
    if (storedLogin.empty() || !HexToBytes(ReadIni(file, L"admin", L"salt", L""), salt) ||
        !HexToBytes(ReadIni(file, L"admin", L"password_hash", L""), storedHash)) {
        error = L"Файл admin.ini отсутствует или повреждён.";
        return false;
    }

    std::vector<std::uint8_t> actualHash;
    if (!DeriveAdminPasswordHash(password, salt, actualHash) || actualHash.size() != storedHash.size()) {
        error = L"Не удалось проверить пароль администратора.";
        return false;
    }

    std::uint8_t difference = 0;
    for (size_t i = 0; i < actualHash.size(); ++i) {
        difference = static_cast<std::uint8_t>(difference | (actualHash[i] ^ storedHash[i]));
    }
    if (login != storedLogin || difference != 0) {
        error = L"Неверный логин или пароль администратора.";
        return false;
    }
    return true;
}

'''
replace_once(
    "std::string UrlEncode(const std::string& value) {\n",
    admin_helpers + "std::string UrlEncode(const std::string& value) {\n",
    "admin helper insertion",
)

admin_ui = r'''
void OpenAdminWindow();

bool RequireAdmin() {
    if (g_adminAuthenticated) return true;
    OpenAdminWindow();
    return false;
}

void OpenAdminWindow() {
    if (g_adminAuthenticated) {
        MessageBoxW(g_mainWindow, (L"Администратор уже вошёл: " + g_adminLogin).c_str(),
            L"ScanDisplay", MB_ICONINFORMATION);
        return;
    }
    if (g_adminWindow && IsWindow(g_adminWindow)) {
        ShowWindow(g_adminWindow, SW_RESTORE);
        SetForegroundWindow(g_adminWindow);
        return;
    }

    g_adminSetupMode = !AdminCredentialsConfigured();
    const int height = g_adminSetupMode ? 390 : 310;
    g_adminWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, kAdminWindowClass,
        g_adminSetupMode ? L"Создание администратора ScanDisplay" : L"Вход администратора ScanDisplay",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, height, g_mainWindow, nullptr, g_instance, nullptr);
    ShowWindow(g_adminWindow, SW_SHOW);
    UpdateWindow(g_adminWindow);
}

LRESULT CALLBACK AdminWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", g_adminSetupMode
                    ? L"Создайте локальную учётную запись администратора:"
                    : L"Введите локальный логин и пароль администратора:",
                WS_CHILD | WS_VISIBLE, 24, 18, 430, 22, window, nullptr, g_instance, nullptr);

            CreateWindowW(L"STATIC", L"Логин:", WS_CHILD | WS_VISIBLE,
                24, 50, 110, 22, window, nullptr, g_instance, nullptr);
            g_adminLoginEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 74, 430, 30, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdminLoginEditId)), g_instance, nullptr);

            CreateWindowW(L"STATIC", L"Пароль:", WS_CHILD | WS_VISIBLE,
                24, 112, 110, 22, window, nullptr, g_instance, nullptr);
            g_adminPasswordEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                24, 136, 430, 30, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdminPasswordEditId)), g_instance, nullptr);

            int buttonY = 184;
            int statusY = 230;
            if (g_adminSetupMode) {
                CreateWindowW(L"STATIC", L"Повторите пароль:", WS_CHILD | WS_VISIBLE,
                    24, 174, 180, 22, window, nullptr, g_instance, nullptr);
                g_adminConfirmEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                    24, 198, 430, 30, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdminConfirmEditId)), g_instance, nullptr);
                buttonY = 244;
                statusY = 290;
            }

            CreateWindowW(L"BUTTON", g_adminSetupMode ? L"Создать администратора" : L"Войти",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                24, buttonY, 220, 34, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdminButtonId)), g_instance, nullptr);
            g_adminStatus = CreateWindowW(L"STATIC",
                g_adminSetupMode
                    ? L"Файл admin.ini будет создан рядом с ScanDisplay.exe. Пароль сохраняется только как защищённый хэш."
                    : L"До входа функции клиента заблокированы.",
                WS_CHILD | WS_VISIBLE, 24, statusY, 430, 50, window, nullptr, g_instance, nullptr);

            SendMessageW(g_adminLoginEdit, EM_SETLIMITTEXT, 64, 0);
            SendMessageW(g_adminPasswordEdit, EM_SETLIMITTEXT, 128, 0);
            if (g_adminConfirmEdit) SendMessageW(g_adminConfirmEdit, EM_SETLIMITTEXT, 128, 0);

            if (!g_adminSetupMode) {
                const std::wstring savedLogin = ReadIni(AdminFile(), L"admin", L"login", L"");
                SetWindowTextW(g_adminLoginEdit, savedLogin.c_str());
            }
            SetFocus(g_adminLoginEdit);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == kAdminButtonId) {
                wchar_t loginBuffer[128]{};
                wchar_t passwordBuffer[256]{};
                wchar_t confirmBuffer[256]{};
                GetWindowTextW(g_adminLoginEdit, loginBuffer, _countof(loginBuffer));
                GetWindowTextW(g_adminPasswordEdit, passwordBuffer, _countof(passwordBuffer));
                if (g_adminConfirmEdit) {
                    GetWindowTextW(g_adminConfirmEdit, confirmBuffer, _countof(confirmBuffer));
                }

                const std::wstring login = TrimText(loginBuffer);
                const std::wstring password = passwordBuffer;
                const std::wstring confirmation = confirmBuffer;
                std::wstring error;
                bool ok = false;

                if (g_adminSetupMode) {
                    if (password != confirmation) {
                        error = L"Пароли не совпадают.";
                    } else {
                        ok = SaveAdminCredentials(login, password, error);
                    }
                } else {
                    ok = VerifyAdminCredentials(login, password, error);
                }

                SecureZeroMemory(passwordBuffer, sizeof(passwordBuffer));
                SecureZeroMemory(confirmBuffer, sizeof(confirmBuffer));

                if (ok) {
                    g_adminAuthenticated = true;
                    g_adminLogin = login;
                    Notify(L"ScanDisplay", L"Администратор вошёл: " + login);
                    DestroyWindow(window);
                } else {
                    SetWindowTextW(g_adminStatus, error.c_str());
                    SetWindowTextW(g_adminPasswordEdit, L"");
                    if (g_adminConfirmEdit) SetWindowTextW(g_adminConfirmEdit, L"");
                    SetFocus(g_adminPasswordEdit);
                }
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            g_adminWindow = nullptr;
            g_adminLoginEdit = nullptr;
            g_adminPasswordEdit = nullptr;
            g_adminConfirmEdit = nullptr;
            g_adminStatus = nullptr;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

'''
replace_once(
    "void ShowTrayMenu(HWND window) {\n",
    admin_ui + "void ShowTrayMenu(HWND window) {\n",
    "admin UI insertion",
)

start = source.index("void ShowTrayMenu(HWND window) {")
end = source.index("\nvoid OpenAuthWindow()", start)
new_menu = r'''void ShowTrayMenu(HWND window) {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();

    StudentSession student;
    {
        std::lock_guard<std::mutex> lock(g_studentMutex);
        student = g_student;
    }
    const AppState state = g_state.load();
    const bool adminReady = g_adminAuthenticated;
    const std::wstring adminCaption = adminReady
        ? L"Администратор: " + g_adminLogin
        : (AdminCredentialsConfigured() ? L"Вход администратора" : L"Создать администратора");
    const std::wstring authCaption = student.authorized()
        ? L"Авторизация студента: " + student.fullName()
        : L"Авторизация студента";

    AppendMenuW(menu, MF_STRING | (adminReady ? MF_GRAYED : MF_ENABLED),
        kMenuAdmin, adminCaption.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (adminReady && state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuAuth, authCaption.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (adminReady && state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuStart, L"Начать запись");
    AppendMenuW(menu, MF_STRING | (adminReady && state == AppState::Recording ? MF_ENABLED : MF_GRAYED),
        kMenuStop, L"Остановить запись");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (adminReady && state == AppState::Idle ? MF_ENABLED : MF_GRAYED),
        kMenuExit, L"Выход");

    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
}
'''
source = source[:start] + new_menu + source[end:]

replace_once(
    """void OpenAuthWindow() {
    if (g_state.load() != AppState::Idle) return;
""",
    """void OpenAuthWindow() {
    if (!RequireAdmin()) return;
    if (g_state.load() != AppState::Idle) return;
""",
    "protect student auth",
)
replace_once(
    """void OpenRecordingTitleWindow() {
    if (g_state.load() != AppState::Idle) return;
""",
    """void OpenRecordingTitleWindow() {
    if (!RequireAdmin()) return;
    if (g_state.load() != AppState::Idle) return;
""",
    "protect recording start",
)
replace_once(
    """void StartRecording(const std::wstring& recordingTitle) {
    if (g_state.load() != AppState::Idle) return;
""",
    """void StartRecording(const std::wstring& recordingTitle) {
    if (!g_adminAuthenticated) return;
    if (g_state.load() != AppState::Idle) return;
""",
    "protect StartRecording",
)
replace_once(
    """void StopRecording() {
    AppState expected = AppState::Recording;
""",
    """void StopRecording() {
    if (!g_adminAuthenticated) return;
    AppState expected = AppState::Recording;
""",
    "protect StopRecording",
)
replace_once(
    """            switch (LOWORD(wParam)) {
                case kMenuAuth: OpenAuthWindow(); return 0;
""",
    """            switch (LOWORD(wParam)) {
                case kMenuAdmin: OpenAdminWindow(); return 0;
                case kMenuAuth: OpenAuthWindow(); return 0;
""",
    "admin menu command",
)
replace_once(
    """                case kMenuExit:
                    if (g_state.load() == AppState::Idle) DestroyWindow(window);
""",
    """                case kMenuExit:
                    if (g_adminAuthenticated && g_state.load() == AppState::Idle) DestroyWindow(window);
""",
    "protect exit",
)
replace_once(
    "    WNDCLASSEXW authClass = mainClass;\n",
    """    WNDCLASSEXW adminClass = mainClass;
    adminClass.lpfnWndProc = AdminWindowProc;
    adminClass.lpszClassName = kAdminWindowClass;
    adminClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    WNDCLASSEXW authClass = mainClass;
""",
    "admin class registration",
)
replace_once(
    """    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0 &&
        RegisterClassExW(&titleClass) != 0;
""",
    """    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&adminClass) != 0 &&
        RegisterClassExW(&authClass) != 0 && RegisterClassExW(&titleClass) != 0;
""",
    "admin register return",
)
replace_once(
    """    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);

    MSG message{};
""",
    """    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);

    OpenAdminWindow();

    MSG message{};
""",
    "open admin at startup",
)

main.write_text(source, encoding="utf-8")

cmake = Path("client/CMakeLists.txt")
cmake_text = cmake.read_text(encoding="utf-8")
if "    bcrypt\n" not in cmake_text:
    marker = "    ole32\n)"
    if marker not in cmake_text:
        raise SystemExit("CMake library marker not found")
    cmake_text = cmake_text.replace(marker, "    ole32\n    bcrypt\n)", 1)
    cmake.write_text(cmake_text, encoding="utf-8")

readme = Path("README.md")
text = readme.read_text(encoding="utf-8")
marker = '''Обычное окно не появится. Значок программы будет находиться в системном трее Windows рядом с часами.

Если значок не виден, нажмите стрелку **Показать скрытые значки**.

Щёлкните по значку правой кнопкой мыши. Доступны пункты:

- **Авторизация**;
- **Начать запись**;
- **Остановить запись**;
- **Выход**.
'''
replacement = '''Обычное основное окно не появится. При первом запуске программа откроет окно создания локального администратора.

Администратор вводит логин, пароль и повтор пароля. После сохранения рядом с `ScanDisplay.exe` автоматически создаётся файл:

```text
admin.ini
```

В `admin.ini` хранятся логин, случайная криптографическая соль и PBKDF2-хэш пароля. Сам пароль в открытом виде в файл не записывается. При следующих запусках администратор должен ввести созданные логин и пароль. До успешного входа авторизация студента, запуск и остановка записи, а также выход через меню заблокированы.

Для полного сброса локального администратора закройте программу и удалите `admin.ini`. При следующем запуске будет предложено создать новую учётную запись. Папка должна разрешать программе запись файла; для переносной установки рекомендуется `C:\\ScanDisplay`.

После входа значок программы находится в системном трее Windows рядом с часами. Если значок не виден, нажмите стрелку **Показать скрытые значки**.

Щёлкните по значку правой кнопкой мыши. Доступны пункты:

- **Администратор**;
- **Авторизация студента**;
- **Начать запись**;
- **Остановить запись**;
- **Выход**.
'''
if marker not in text:
    raise SystemExit("README launch marker not found")
readme.write_text(text.replace(marker, replacement, 1), encoding="utf-8")
