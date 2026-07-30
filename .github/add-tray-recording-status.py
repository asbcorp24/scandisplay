from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"Marker not found: {label}")
    return text.replace(old, new, 1)


main_path = Path("client/src/main.cpp")
source = main_path.read_text(encoding="utf-8")

if "void UpdateTrayTooltip()" not in source:
    source = replace_once(
        source,
        "constexpr UINT kTrayId = 1;\n",
        "constexpr UINT kTrayId = 1;\n"
        "constexpr UINT_PTR kTrayStatusTimerId = 1;\n"
        "constexpr UINT kTrayStatusIntervalMs = 1000;\n",
        "tray timer constants",
    )

    source = replace_once(
        source,
        "std::wstring g_recordingTitle;\n",
        "std::wstring g_recordingTitle;\n"
        "std::chrono::steady_clock::time_point g_recordingStartedSteady{};\n",
        "recording steady start",
    )

    helpers = r'''std::wstring FormatFileSize(std::uintmax_t bytes) {
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

'''
    source = replace_once(
        source,
        "void Notify(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO) {\n",
        helpers + "void Notify(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO) {\n",
        "tray status helpers",
    )

    source = replace_once(
        source,
        "    g_startedAt = TimestampIso();\n"
        "    g_recordingTitle = recordingTitle;\n",
        "    g_startedAt = TimestampIso();\n"
        "    g_recordingTitle = recordingTitle;\n"
        "    g_recordingStartedSteady = std::chrono::steady_clock::now();\n",
        "recording timer start",
    )

    source = replace_once(
        source,
        "    g_stopCapture.store(false);\n"
        "    g_state.store(AppState::Recording);\n",
        "    g_stopCapture.store(false);\n"
        "    g_state.store(AppState::Recording);\n"
        "    UpdateTrayTooltip();\n",
        "initial recording tooltip",
    )

    source = replace_once(
        source,
        "        g_ffmpegProcess = {};\n"
        "        g_state.store(AppState::Idle);\n"
        "        MessageBoxW(g_mainWindow, L\"Не удалось запустить поток записи.\", L\"ScanDisplay\", MB_ICONERROR);\n",
        "        g_ffmpegProcess = {};\n"
        "        g_state.store(AppState::Idle);\n"
        "        UpdateTrayTooltip();\n"
        "        MessageBoxW(g_mainWindow, L\"Не удалось запустить поток записи.\", L\"ScanDisplay\", MB_ICONERROR);\n",
        "recording thread error tooltip",
    )

    source = replace_once(
        source,
        "    if (!g_state.compare_exchange_strong(expected, AppState::Finalizing)) return;\n\n"
        "    g_stopCapture.store(true);\n",
        "    if (!g_state.compare_exchange_strong(expected, AppState::Finalizing)) return;\n"
        "    UpdateTrayTooltip();\n\n"
        "    g_stopCapture.store(true);\n",
        "finalizing tooltip",
    )

    source = replace_once(
        source,
        "            g_state.store(AppState::Idle);\n"
        "            Notify(ok ? L\"Запись отправлена\" : L\"Ошибка записи\",\n",
        "            g_state.store(AppState::Idle);\n"
        "            UpdateTrayTooltip();\n"
        "            Notify(ok ? L\"Запись отправлена\" : L\"Ошибка записи\",\n",
        "idle tooltip after finalization",
    )

    source = replace_once(
        source,
        "        case kTrayMessage: {\n",
        "        case WM_TIMER:\n"
        "            if (wParam == kTrayStatusTimerId) {\n"
        "                UpdateTrayTooltip();\n"
        "                return 0;\n"
        "            }\n"
        "            break;\n\n"
        "        case kTrayMessage: {\n",
        "tray timer message",
    )

    source = replace_once(
        source,
        "        case WM_DESTROY:\n"
        "            Shell_NotifyIconW(NIM_DELETE, &g_tray);\n",
        "        case WM_DESTROY:\n"
        "            KillTimer(window, kTrayStatusTimerId);\n"
        "            Shell_NotifyIconW(NIM_DELETE, &g_tray);\n",
        "kill tray timer",
    )

    source = replace_once(
        source,
        "    g_tray.uVersion = NOTIFYICON_VERSION_4;\n"
        "    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);\n\n"
        "    MSG message{};\n",
        "    g_tray.uVersion = NOTIFYICON_VERSION_4;\n"
        "    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);\n"
        "    SetTimer(g_mainWindow, kTrayStatusTimerId, kTrayStatusIntervalMs, nullptr);\n"
        "    UpdateTrayTooltip();\n\n"
        "    MSG message{};\n",
        "start tray timer",
    )

main_path.write_text(source, encoding="utf-8")

readme_path = Path("README.md")
readme = readme_path.read_text(encoding="utf-8")
status_text = "При наведении курсора на значок во время записи подсказка показывает, сколько минут и секунд идёт запись, а также текущий размер создаваемого MP4-файла. Данные обновляются каждую секунду.\n\n"
if status_text not in readme:
    marker = "- **Выход**.\n\n"
    if marker not in readme:
        raise SystemExit("README tray menu marker not found")
    readme = readme.replace(marker, marker + status_text, 1)
readme_path.write_text(readme, encoding="utf-8")

print("Tray recording duration and file size tooltip added.")
