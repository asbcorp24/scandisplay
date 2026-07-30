from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"Expected fragment not found ({label}): {old[:180]!r}")
    return text.replace(old, new, 1)


# Windows client
main_path = Path("client/src/main.cpp")
source = main_path.read_text(encoding="utf-8")

source = replace_once(
    source,
    'constexpr wchar_t kMainWindowClass[] = L"ScanDisplayMainWindow";\nconstexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";',
    'constexpr wchar_t kMainWindowClass[] = L"ScanDisplayMainWindow";\nconstexpr wchar_t kAuthWindowClass[] = L"ScanDisplayAuthWindow";\nconstexpr wchar_t kRecordingTitleWindowClass[] = L"ScanDisplayRecordingTitleWindow";',
    "window classes",
)
source = replace_once(
    source,
    'constexpr UINT kAuthEditId = 2001;\nconstexpr UINT kAuthButtonId = 2002;',
    'constexpr UINT kAuthEditId = 2001;\nconstexpr UINT kAuthButtonId = 2002;\nconstexpr UINT kRecordingTitleEditId = 2101;\nconstexpr UINT kRecordingTitleButtonId = 2102;',
    "title control ids",
)
source = replace_once(
    source,
    'HWND g_authWindow = nullptr;\nHWND g_authEdit = nullptr;\nHWND g_authStatus = nullptr;',
    'HWND g_authWindow = nullptr;\nHWND g_authEdit = nullptr;\nHWND g_authStatus = nullptr;\nHWND g_recordingTitleWindow = nullptr;\nHWND g_recordingTitleEdit = nullptr;\nHWND g_recordingTitleStatus = nullptr;',
    "title globals",
)
source = replace_once(
    source,
    'fs::path g_outputFile;\nstd::wstring g_startedAt;',
    'fs::path g_outputFile;\nstd::wstring g_startedAt;\nstd::wstring g_recordingTitle;',
    "recording title state",
)
source = replace_once(
    source,
    'std::wstring ModuleDirectory() {\n    std::vector<wchar_t> buffer(32768);\n    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));\n    return fs::path(std::wstring(buffer.data(), length)).parent_path().wstring();\n}\n',
    'std::wstring ModuleDirectory() {\n    std::vector<wchar_t> buffer(32768);\n    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));\n    return fs::path(std::wstring(buffer.data(), length)).parent_path().wstring();\n}\n\nstd::wstring TrimText(const std::wstring& value) {\n    const size_t first = value.find_first_not_of(L" \\t\\r\\n");\n    if (first == std::wstring::npos) return {};\n    const size_t last = value.find_last_not_of(L" \\t\\r\\n");\n    return value.substr(first, last - first + 1);\n}\n',
    "trim helper",
)
source = replace_once(
    source,
    'bool UploadVideo(const fs::path& file, const std::wstring& startedAt, const std::wstring& endedAt,\n                 const StudentSession& student, std::wstring& error) {',
    'bool UploadVideo(const fs::path& file, const std::wstring& startedAt, const std::wstring& endedAt,\n                 const std::wstring& recordingTitle, const StudentSession& student, std::wstring& error) {',
    "upload signature",
)
source = replace_once(
    source,
    '    std::string prefix;\n    prefix += field("started_at", startedAt);',
    '    std::string prefix;\n    prefix += field("title", recordingTitle);\n    prefix += field("started_at", startedAt);',
    "upload title field",
)
source = replace_once(
    source,
    'void FinalizeRecording(fs::path outputFile, std::wstring startedAt, std::wstring endedAt,\n                       StudentSession student) {',
    'void FinalizeRecording(fs::path outputFile, std::wstring startedAt, std::wstring endedAt,\n                       std::wstring recordingTitle, StudentSession student) {',
    "finalize signature",
)
source = replace_once(
    source,
    '    if (ok) ok = UploadVideo(outputFile, startedAt, endedAt, student, error);',
    '    if (ok) ok = UploadVideo(outputFile, startedAt, endedAt, recordingTitle, student, error);',
    "upload title call",
)
source = replace_once(source, 'void StartRecording() {', 'void StartRecording(const std::wstring& recordingTitle) {', "start signature")
source = replace_once(
    source,
    '    g_startedAt = TimestampIso();\n    g_recordingStudent = student;',
    '    g_startedAt = TimestampIso();\n    g_recordingTitle = recordingTitle;\n    g_recordingStudent = student;',
    "save title",
)
source = replace_once(
    source,
    '    Notify(L"ScanDisplay", L"Запись экрана начата. Интервал кадров: " +\n        std::to_wstring(g_config.captureIntervalSeconds) + L" сек.");',
    '    Notify(L"ScanDisplay", L"Запись начата: " + recordingTitle + L". Интервал кадров: " +\n        std::to_wstring(g_config.captureIntervalSeconds) + L" сек.");',
    "start notification",
)
source = replace_once(
    source,
    '    g_finalizeThread = std::thread(FinalizeRecording, g_outputFile, g_startedAt, endedAt, g_recordingStudent);',
    '    g_finalizeThread = std::thread(FinalizeRecording, g_outputFile, g_startedAt, endedAt,\n        g_recordingTitle, g_recordingStudent);',
    "finalize title",
)

title_window = r'''void OpenRecordingTitleWindow() {
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

'''
source = replace_once(
    source,
    'LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {',
    title_window + 'LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {',
    "title window",
)
source = replace_once(source, '                case kMenuStart: StartRecording(); return 0;', '                case kMenuStart: OpenRecordingTitleWindow(); return 0;', "menu start")
source = replace_once(
    source,
    '    WNDCLASSEXW authClass = mainClass;\n    authClass.lpfnWndProc = AuthWindowProc;\n    authClass.lpszClassName = kAuthWindowClass;\n    authClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);\n    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0;',
    '    WNDCLASSEXW authClass = mainClass;\n    authClass.lpfnWndProc = AuthWindowProc;\n    authClass.lpszClassName = kAuthWindowClass;\n    authClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);\n\n    WNDCLASSEXW titleClass = mainClass;\n    titleClass.lpfnWndProc = RecordingTitleWindowProc;\n    titleClass.lpszClassName = kRecordingTitleWindowClass;\n    titleClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);\n\n    return RegisterClassExW(&mainClass) != 0 && RegisterClassExW(&authClass) != 0 &&\n        RegisterClassExW(&titleClass) != 0;',
    "register title window",
)
main_path.write_text(source, encoding="utf-8")


# Migration helper
bootstrap_path = Path("server/bootstrap.php")
bootstrap = bootstrap_path.read_text(encoding="utf-8")
migration = r'''function ensure_recordings_title_column(): void
{
    static $done = false;
    if ($done) return;

    $pdo = db();
    $driver = $pdo->getAttribute(PDO::ATTR_DRIVER_NAME);
    if ($driver === 'sqlite') {
        $columns = $pdo->query('PRAGMA table_info(recordings)')->fetchAll();
        $hasTitle = false;
        foreach ($columns as $column) {
            if (($column['name'] ?? '') === 'title') {
                $hasTitle = true;
                break;
            }
        }
        if (!$hasTitle) {
            $pdo->exec("ALTER TABLE recordings ADD COLUMN title TEXT NOT NULL DEFAULT 'Без названия'");
        }
    } else {
        $stmt = $pdo->query("SHOW COLUMNS FROM recordings LIKE 'title'");
        if (!$stmt->fetch()) {
            $pdo->exec("ALTER TABLE recordings ADD COLUMN title VARCHAR(255) NOT NULL DEFAULT 'Без названия' AFTER student_id");
        }
    }
    $done = true;
}

'''
bootstrap = replace_once(bootstrap, 'function h(?string $value): string\n{', migration + 'function h(?string $value): string\n{', "migration helper")
bootstrap_path.write_text(bootstrap, encoding="utf-8")


# Installer
install_path = Path("server/install.php")
install = install_path.read_text(encoding="utf-8")
install = replace_once(install, '                        student_id BIGINT UNSIGNED NOT NULL,\n                        file_path VARCHAR(1024) NOT NULL,', '                        student_id BIGINT UNSIGNED NOT NULL,\n                        title VARCHAR(255) NOT NULL,\n                        file_path VARCHAR(1024) NOT NULL,', "mysql title")
install = replace_once(install, '                        student_id INTEGER NOT NULL,\n                        file_path TEXT NOT NULL,', '                        student_id INTEGER NOT NULL,\n                        title TEXT NOT NULL,\n                        file_path TEXT NOT NULL,', "sqlite title")
install_path.write_text(install, encoding="utf-8")


# Upload API
upload_path = Path("server/api/upload.php")
upload = upload_path.read_text(encoding="utf-8")
upload = replace_once(upload, "if (!is_installed()) {\n    json_response(['ok' => false, 'message' => 'Сервер не установлен.'], 503);\n}\n", "if (!is_installed()) {\n    json_response(['ok' => false, 'message' => 'Сервер не установлен.'], 503);\n}\nensure_recordings_title_column();\n", "upload migration")
upload = replace_once(upload, "$computerName = trim((string) ($_POST['computer_name'] ?? $session['authorized_computer']));", "$title = preg_replace('/\\s+/u', ' ', trim((string) ($_POST['title'] ?? ''))) ?? '';\nif ($title === '') {\n    json_response(['ok' => false, 'message' => 'Не указано название записи.'], 422);\n}\n$title = mb_substr($title, 0, 255);\n\n$computerName = trim((string) ($_POST['computer_name'] ?? $session['authorized_computer']));", "upload title validation")
upload = replace_once(upload, "        'INSERT INTO recordings (student_id, file_path, original_name, file_size, started_at, ended_at, computer_name, created_at)\n         VALUES (?, ?, ?, ?, ?, ?, ?, ?)'", "        'INSERT INTO recordings (student_id, title, file_path, original_name, file_size, started_at, ended_at, computer_name, created_at)\n         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)'", "upload insert")
upload = replace_once(upload, '        $studentId,\n        $relativePath,', '        $studentId,\n        $title,\n        $relativePath,', "upload title value")
upload_path.write_text(upload, encoding="utf-8")


# Admin UI
index_path = Path("server/index.php")
index = index_path.read_text(encoding="utf-8")
index = replace_once(index, "if (!is_installed()) {\n    header('Location: install.php');\n    exit;\n}\n\n$pdo = db();", "if (!is_installed()) {\n    header('Location: install.php');\n    exit;\n}\nensure_recordings_title_column();\n\n$pdo = db();", "index migration")
index = replace_once(index, "$dateFrom = trim((string) ($_GET['date_from'] ?? ''));\n$dateTo = trim((string) ($_GET['date_to'] ?? ''));", "$titleQuery = mb_substr(trim((string) ($_GET['title'] ?? '')), 0, 255);\n$dateFrom = trim((string) ($_GET['date_from'] ?? ''));\n$dateTo = trim((string) ($_GET['date_to'] ?? ''));", "title query")
index = replace_once(index, "    if ($selectedStudent > 0) { $where[] = 'r.student_id = ?'; $params[] = $selectedStudent; }\n    if ($dateFrom !== '') {", "    if ($selectedStudent > 0) { $where[] = 'r.student_id = ?'; $params[] = $selectedStudent; }\n    if ($titleQuery !== '') { $where[] = 'r.title LIKE ?'; $params[] = '%' . $titleQuery . '%'; }\n    if ($dateFrom !== '') {", "title filter")
index = replace_once(index, '<thead><tr><th>Дата</th><th>Группа</th><th>Студент</th><th>Компьютер</th><th>Размер</th><th></th></tr></thead>', '<thead><tr><th>Дата</th><th>Название</th><th>Группа</th><th>Студент</th><th>Компьютер</th><th>Размер</th><th></th></tr></thead>', "dashboard heading")
index = replace_once(index, "                        <td><?= h($record['created_at']) ?></td>\n                        <td><?= h($record['group_name']) ?></td>", "                        <td><?= h($record['created_at']) ?></td>\n                        <td><strong><?= h($record['title']) ?></strong></td>\n                        <td><?= h($record['group_name']) ?></td>", "dashboard title")
index = replace_once(index, '<?php if (!$recent): ?><tr><td colspan="6" class="text-center text-secondary py-5">Записей пока нет</td></tr><?php endif; ?>', '<?php if (!$recent): ?><tr><td colspan="7" class="text-center text-secondary py-5">Записей пока нет</td></tr><?php endif; ?>', "dashboard colspan")
old_filters = '''                <div class="col-md-3"><label class="form-label">Группа</label><select class="form-select" name="group_id"><option value="0">Все группы</option><?php foreach ($groupOptions as $group): ?><option value="<?= (int) $group['id'] ?>" <?= $selectedGroup === (int) $group['id'] ? 'selected' : '' ?>><?= h($group['name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-3"><label class="form-label">Студент</label><select class="form-select" name="student_id"><option value="0">Все студенты</option><?php foreach ($recordingStudents as $student): ?><option value="<?= (int) $student['id'] ?>" <?= $selectedStudent === (int) $student['id'] ? 'selected' : '' ?>><?= h($student['group_name'] . ' — ' . $student['last_name'] . ' ' . $student['first_name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-2"><label class="form-label">С даты</label><input class="form-control" type="date" name="date_from" value="<?= h($dateFrom) ?>"></div>
                <div class="col-md-2"><label class="form-label">По дату</label><input class="form-control" type="date" name="date_to" value="<?= h($dateTo) ?>"></div>
                <div class="col-md-2"><button class="btn btn-primary w-100">Применить</button></div>'''
new_filters = '''                <div class="col-md-4"><label class="form-label">Название записи</label><input class="form-control" name="title" value="<?= h($titleQuery) ?>" placeholder="Например, Лабораторная работа № 2"></div>
                <div class="col-md-4"><label class="form-label">Группа</label><select class="form-select" name="group_id"><option value="0">Все группы</option><?php foreach ($groupOptions as $group): ?><option value="<?= (int) $group['id'] ?>" <?= $selectedGroup === (int) $group['id'] ? 'selected' : '' ?>><?= h($group['name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-4"><label class="form-label">Студент</label><select class="form-select" name="student_id"><option value="0">Все студенты</option><?php foreach ($recordingStudents as $student): ?><option value="<?= (int) $student['id'] ?>" <?= $selectedStudent === (int) $student['id'] ? 'selected' : '' ?>><?= h($student['group_name'] . ' — ' . $student['last_name'] . ' ' . $student['first_name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-3"><label class="form-label">С даты</label><input class="form-control" type="date" name="date_from" value="<?= h($dateFrom) ?>"></div>
                <div class="col-md-3"><label class="form-label">По дату</label><input class="form-control" type="date" name="date_to" value="<?= h($dateTo) ?>"></div>
                <div class="col-md-3"><button class="btn btn-primary w-100">Применить</button></div>
                <div class="col-md-3"><a class="btn btn-outline-secondary w-100" href="?page=recordings">Сбросить</a></div>'''
index = replace_once(index, old_filters, new_filters, "archive filters")
index = replace_once(index, "                                <div><h2 class=\"h5 mb-1\"><?= h($record['last_name'] . ' ' . $record['first_name']) ?></h2><div class=\"text-secondary\"><?= h($record['group_name']) ?> · код <?= h($record['code']) ?></div></div>", "                                <div><h2 class=\"h5 mb-1\"><?= h($record['title']) ?></h2><div class=\"fw-semibold\"><?= h($record['last_name'] . ' ' . $record['first_name']) ?></div><div class=\"text-secondary\"><?= h($record['group_name']) ?> · код <?= h($record['code']) ?></div></div>", "card title")
index_path.write_text(index, encoding="utf-8")


# Stream/download
stream_path = Path("server/stream.php")
stream = stream_path.read_text(encoding="utf-8")
stream = replace_once(stream, 'require_admin();\n\n$id =', 'require_admin();\nensure_recordings_title_column();\n\n$id =', "stream migration")
stream = replace_once(stream, "$stmt = db()->prepare('SELECT id, file_path, original_name FROM recordings WHERE id = ?');", "$stmt = db()->prepare('SELECT id, title, file_path, original_name FROM recordings WHERE id = ?');", "stream title")
stream = replace_once(stream, "$downloadName = preg_replace('/[^\\pL\\pN._-]+/u', '_', (string) $recording['original_name']) ?: 'recording.mp4';", "$sourceName = $download ? ((string) $recording['title'] . '.mp4') : (string) $recording['original_name'];\n$downloadName = preg_replace('/[^\\pL\\pN._-]+/u', '_', $sourceName) ?: 'recording.mp4';", "download title")
stream_path.write_text(stream, encoding="utf-8")


# README
readme_path = Path("README.md")
readme = readme_path.read_text(encoding="utf-8")
readme = readme.replace('2. Выберите **Начать запись**.\n3. Работайте за компьютером.', '2. Выберите **Начать запись**.\n3. Введите обязательное название работы или задания и нажмите **Начать запись**.\n4. Работайте за компьютером.')
readme = readme.replace('4. Программа будет получать новый кадр', '5. Программа будет получать новый кадр')
readme = readme.replace('5. В верхней части каждого кадра будут записаны:', '6. В верхней части каждого кадра будут записаны:')
readme = readme.replace('6. Для завершения нажмите **Остановить запись**.', '7. Для завершения нажмите **Остановить запись**.')
readme = readme.replace('started_at\nended_at\ncomputer_name\nvideo', 'title\nstarted_at\nended_at\ncomputer_name\nvideo')
readme = readme.replace('Там можно:\n\n- отфильтровать записи по группе;', 'Там можно:\n\n- искать записи по введённому студентом названию;\n- отфильтровать записи по группе;')
readme_path.write_text(readme, encoding="utf-8")
