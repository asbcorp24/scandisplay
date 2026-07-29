<?php

declare(strict_types=1);

require __DIR__ . '/bootstrap.php';

if (!is_installed()) {
    header('Location: install.php');
    exit;
}

$pdo = db();
$loginError = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST' && ($_POST['action'] ?? '') === 'login') {
    require_csrf();
    $username = trim((string) ($_POST['username'] ?? ''));
    $password = (string) ($_POST['password'] ?? '');

    $stmt = $pdo->prepare('SELECT * FROM admins WHERE username = ? LIMIT 1');
    $stmt->execute([$username]);
    $admin = $stmt->fetch();

    if ($admin && password_verify($password, $admin['password_hash'])) {
        session_regenerate_id(true);
        $_SESSION['admin_id'] = (int) $admin['id'];
        $_SESSION['admin_username'] = $admin['username'];
        header('Location: index.php');
        exit;
    }
    $loginError = 'Неверный логин или пароль.';
}

if (empty($_SESSION['admin_id'])):
?>
<!doctype html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Вход — ScanDisplay</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/css/bootstrap.min.css" rel="stylesheet">
</head>
<body class="bg-body-tertiary">
<main class="container py-5" style="max-width:520px">
    <div class="card border-0 shadow-sm mt-lg-5">
        <div class="card-body p-4 p-md-5">
            <h1 class="h3 mb-1">ScanDisplay</h1>
            <p class="text-secondary mb-4">Администрирование записей экрана</p>
            <?php $flash = take_flash(); if ($flash): ?>
                <div class="alert alert-<?= h($flash['type']) ?>"><?= h($flash['message']) ?></div>
            <?php endif; ?>
            <?php if ($loginError !== ''): ?>
                <div class="alert alert-danger"><?= h($loginError) ?></div>
            <?php endif; ?>
            <form method="post">
                <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                <input type="hidden" name="action" value="login">
                <div class="mb-3">
                    <label class="form-label" for="username">Логин</label>
                    <input class="form-control" id="username" name="username" required autofocus>
                </div>
                <div class="mb-4">
                    <label class="form-label" for="password">Пароль</label>
                    <input class="form-control" id="password" name="password" type="password" required>
                </div>
                <button class="btn btn-primary w-100">Войти</button>
            </form>
        </div>
    </div>
</main>
</body>
</html>
<?php
exit;
endif;

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    require_csrf();
    $action = (string) ($_POST['action'] ?? '');
    $redirectPage = (string) ($_POST['redirect_page'] ?? 'dashboard');
    if (!in_array($redirectPage, ['dashboard', 'groups', 'students', 'recordings'], true)) {
        $redirectPage = 'dashboard';
    }

    try {
        if ($action === 'logout') {
            $_SESSION = [];
            session_destroy();
            header('Location: index.php');
            exit;
        }

        if ($action === 'create_group') {
            $name = trim((string) ($_POST['name'] ?? ''));
            if ($name === '') throw new RuntimeException('Введите название группы.');
            $stmt = $pdo->prepare('INSERT INTO student_groups (name, created_at) VALUES (?, ?)');
            $stmt->execute([$name, date('Y-m-d H:i:s')]);
            flash('success', 'Группа создана.');
        } elseif ($action === 'rename_group') {
            $id = (int) ($_POST['id'] ?? 0);
            $name = trim((string) ($_POST['name'] ?? ''));
            if ($id <= 0 || $name === '') throw new RuntimeException('Некорректные данные группы.');
            $pdo->prepare('UPDATE student_groups SET name = ? WHERE id = ?')->execute([$name, $id]);
            flash('success', 'Название группы сохранено.');
        } elseif ($action === 'delete_group') {
            $id = (int) ($_POST['id'] ?? 0);
            $stmt = $pdo->prepare('SELECT COUNT(*) FROM students WHERE group_id = ?');
            $stmt->execute([$id]);
            if ((int) $stmt->fetchColumn() > 0) {
                throw new RuntimeException('Нельзя удалить группу, пока в ней есть студенты.');
            }
            $pdo->prepare('DELETE FROM student_groups WHERE id = ?')->execute([$id]);
            flash('success', 'Группа удалена.');
        } elseif ($action === 'create_student') {
            $groupId = (int) ($_POST['group_id'] ?? 0);
            $firstName = trim((string) ($_POST['first_name'] ?? ''));
            $lastName = trim((string) ($_POST['last_name'] ?? ''));
            if ($groupId <= 0 || $firstName === '' || $lastName === '') {
                throw new RuntimeException('Заполните группу, фамилию и имя.');
            }
            $code = generate_student_code($pdo);
            $stmt = $pdo->prepare(
                'INSERT INTO students (group_id, first_name, last_name, code, active, created_at)
                 VALUES (?, ?, ?, ?, 1, ?)'
            );
            $stmt->execute([$groupId, $firstName, $lastName, $code, date('Y-m-d H:i:s')]);
            flash('success', 'Студент создан. Цифровой код: ' . $code);
        } elseif ($action === 'update_student') {
            $id = (int) ($_POST['id'] ?? 0);
            $groupId = (int) ($_POST['group_id'] ?? 0);
            $firstName = trim((string) ($_POST['first_name'] ?? ''));
            $lastName = trim((string) ($_POST['last_name'] ?? ''));
            $active = (int) ($_POST['active'] ?? 0) === 1 ? 1 : 0;
            if ($id <= 0 || $groupId <= 0 || $firstName === '' || $lastName === '') {
                throw new RuntimeException('Некорректные данные студента.');
            }
            $stmt = $pdo->prepare(
                'UPDATE students SET group_id = ?, first_name = ?, last_name = ?, active = ? WHERE id = ?'
            );
            $stmt->execute([$groupId, $firstName, $lastName, $active, $id]);
            if ($active === 0) {
                $pdo->prepare('DELETE FROM student_sessions WHERE student_id = ?')->execute([$id]);
            }
            flash('success', 'Данные студента сохранены.');
        } elseif ($action === 'regenerate_code') {
            $id = (int) ($_POST['id'] ?? 0);
            if ($id <= 0) throw new RuntimeException('Студент не найден.');
            $code = generate_student_code($pdo);
            $pdo->prepare('UPDATE students SET code = ? WHERE id = ?')->execute([$code, $id]);
            $pdo->prepare('DELETE FROM student_sessions WHERE student_id = ?')->execute([$id]);
            flash('warning', 'Создан новый код: ' . $code . '. Старые авторизации отменены.');
        } elseif ($action === 'delete_recording') {
            $id = (int) ($_POST['id'] ?? 0);
            $stmt = $pdo->prepare('SELECT file_path FROM recordings WHERE id = ?');
            $stmt->execute([$id]);
            $recording = $stmt->fetch();
            if (!$recording) throw new RuntimeException('Запись не найдена.');

            $absolute = rtrim((string) config('upload_dir'), '/\\') . DIRECTORY_SEPARATOR . $recording['file_path'];
            if (is_file($absolute)) @unlink($absolute);
            $pdo->prepare('DELETE FROM recordings WHERE id = ?')->execute([$id]);
            flash('success', 'Запись удалена.');
        }
    } catch (Throwable $exception) {
        flash('danger', $exception->getMessage());
    }

    header('Location: index.php?page=' . rawurlencode($redirectPage));
    exit;
}

$page = (string) ($_GET['page'] ?? 'dashboard');
if (!in_array($page, ['dashboard', 'groups', 'students', 'recordings'], true)) {
    $page = 'dashboard';
}

$groups = $pdo->query(
    'SELECT g.id, g.name, g.created_at, COUNT(s.id) AS student_count
     FROM student_groups g
     LEFT JOIN students s ON s.group_id = g.id
     GROUP BY g.id, g.name, g.created_at
     ORDER BY g.name'
)->fetchAll();
$groupOptions = $pdo->query('SELECT id, name FROM student_groups ORDER BY name')->fetchAll();
$flash = take_flash();

$counts = [
    'groups' => (int) $pdo->query('SELECT COUNT(*) FROM student_groups')->fetchColumn(),
    'students' => (int) $pdo->query('SELECT COUNT(*) FROM students WHERE active = 1')->fetchColumn(),
    'recordings' => (int) $pdo->query('SELECT COUNT(*) FROM recordings')->fetchColumn(),
    'size' => (int) $pdo->query('SELECT COALESCE(SUM(file_size), 0) FROM recordings')->fetchColumn(),
];

$students = [];
if ($page === 'students') {
    $students = $pdo->query(
        'SELECT s.*, g.name AS group_name,
            (SELECT COUNT(*) FROM recordings r WHERE r.student_id = s.id) AS recording_count
         FROM students s
         JOIN student_groups g ON g.id = s.group_id
         ORDER BY g.name, s.last_name, s.first_name'
    )->fetchAll();
}

$recordings = [];
$recordingStudents = [];
$selectedGroup = (int) ($_GET['group_id'] ?? 0);
$selectedStudent = (int) ($_GET['student_id'] ?? 0);
$dateFrom = trim((string) ($_GET['date_from'] ?? ''));
$dateTo = trim((string) ($_GET['date_to'] ?? ''));

if ($page === 'recordings') {
    $recordingStudents = $pdo->query(
        'SELECT s.id, s.first_name, s.last_name, g.name AS group_name
         FROM students s
         JOIN student_groups g ON g.id = s.group_id
         ORDER BY g.name, s.last_name, s.first_name'
    )->fetchAll();

    $where = [];
    $params = [];
    if ($selectedGroup > 0) { $where[] = 's.group_id = ?'; $params[] = $selectedGroup; }
    if ($selectedStudent > 0) { $where[] = 'r.student_id = ?'; $params[] = $selectedStudent; }
    if ($dateFrom !== '') { $where[] = 'r.created_at >= ?'; $params[] = $dateFrom . ' 00:00:00'; }
    if ($dateTo !== '') { $where[] = 'r.created_at <= ?'; $params[] = $dateTo . ' 23:59:59'; }

    $sql = 'SELECT r.*, s.first_name, s.last_name, s.code, g.name AS group_name
            FROM recordings r
            JOIN students s ON s.id = r.student_id
            JOIN student_groups g ON g.id = s.group_id';
    if ($where) $sql .= ' WHERE ' . implode(' AND ', $where);
    $sql .= ' ORDER BY r.created_at DESC LIMIT 500';
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $recordings = $stmt->fetchAll();
}

$recent = [];
if ($page === 'dashboard') {
    $recent = $pdo->query(
        'SELECT r.*, s.first_name, s.last_name, g.name AS group_name
         FROM recordings r
         JOIN students s ON s.id = r.student_id
         JOIN student_groups g ON g.id = s.group_id
         ORDER BY r.created_at DESC LIMIT 10'
    )->fetchAll();
}
?>
<!doctype html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ScanDisplay</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background:#f5f7fb; }
        .navbar-brand { font-weight:750; letter-spacing:-.02em; }
        .stat-card { border:0; box-shadow:0 .25rem 1rem rgba(24,39,75,.06); }
        .code { font:700 1rem ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; letter-spacing:.08em; }
        .table td,.table th { vertical-align:middle; }
        .recording-card video { width:100%; max-height:520px; background:#111; }
    </style>
</head>
<body>
<nav class="navbar navbar-expand-lg bg-dark navbar-dark shadow-sm">
    <div class="container-fluid px-lg-4">
        <a class="navbar-brand" href="index.php">ScanDisplay</a>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#nav"><span class="navbar-toggler-icon"></span></button>
        <div class="collapse navbar-collapse" id="nav">
            <ul class="navbar-nav me-auto">
                <li class="nav-item"><a class="nav-link <?= $page === 'dashboard' ? 'active' : '' ?>" href="index.php">Главная</a></li>
                <li class="nav-item"><a class="nav-link <?= $page === 'groups' ? 'active' : '' ?>" href="?page=groups">Группы</a></li>
                <li class="nav-item"><a class="nav-link <?= $page === 'students' ? 'active' : '' ?>" href="?page=students">Студенты</a></li>
                <li class="nav-item"><a class="nav-link <?= $page === 'recordings' ? 'active' : '' ?>" href="?page=recordings">Видеозаписи</a></li>
            </ul>
            <span class="navbar-text me-3"><?= h($_SESSION['admin_username'] ?? '') ?></span>
            <form method="post">
                <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                <input type="hidden" name="action" value="logout">
                <button class="btn btn-outline-light btn-sm">Выйти</button>
            </form>
        </div>
    </div>
</nav>

<main class="container-fluid px-lg-4 py-4">
    <?php if ($flash): ?>
        <div class="alert alert-<?= h($flash['type']) ?> alert-dismissible fade show">
            <?= h($flash['message']) ?>
            <button class="btn-close" data-bs-dismiss="alert"></button>
        </div>
    <?php endif; ?>

    <?php if ($page === 'dashboard'): ?>
        <div class="mb-4"><h1 class="h3 mb-1">Обзор</h1><div class="text-secondary">Состояние системы записи экранов</div></div>
        <div class="row g-3 mb-4">
            <div class="col-sm-6 col-xl-3"><div class="card stat-card"><div class="card-body"><div class="text-secondary">Группы</div><div class="display-6 fw-semibold"><?= $counts['groups'] ?></div></div></div></div>
            <div class="col-sm-6 col-xl-3"><div class="card stat-card"><div class="card-body"><div class="text-secondary">Активные студенты</div><div class="display-6 fw-semibold"><?= $counts['students'] ?></div></div></div></div>
            <div class="col-sm-6 col-xl-3"><div class="card stat-card"><div class="card-body"><div class="text-secondary">Видеозаписи</div><div class="display-6 fw-semibold"><?= $counts['recordings'] ?></div></div></div></div>
            <div class="col-sm-6 col-xl-3"><div class="card stat-card"><div class="card-body"><div class="text-secondary">Объём архива</div><div class="h2 fw-semibold mt-2"><?= h(format_bytes($counts['size'])) ?></div></div></div></div>
        </div>
        <div class="card border-0 shadow-sm">
            <div class="card-header bg-white py-3 d-flex justify-content-between"><strong>Последние записи</strong><a href="?page=recordings">Открыть архив</a></div>
            <div class="table-responsive"><table class="table table-hover mb-0">
                <thead><tr><th>Дата</th><th>Группа</th><th>Студент</th><th>Компьютер</th><th>Размер</th><th></th></tr></thead>
                <tbody>
                <?php foreach ($recent as $record): ?>
                    <tr>
                        <td><?= h($record['created_at']) ?></td>
                        <td><?= h($record['group_name']) ?></td>
                        <td><?= h($record['last_name'] . ' ' . $record['first_name']) ?></td>
                        <td><?= h($record['computer_name']) ?></td>
                        <td><?= h(format_bytes((int) $record['file_size'])) ?></td>
                        <td><a class="btn btn-sm btn-outline-primary" href="?page=recordings&student_id=<?= (int) $record['student_id'] ?>">Смотреть</a></td>
                    </tr>
                <?php endforeach; ?>
                <?php if (!$recent): ?><tr><td colspan="6" class="text-center text-secondary py-5">Записей пока нет</td></tr><?php endif; ?>
                </tbody>
            </table></div>
        </div>

    <?php elseif ($page === 'groups'): ?>
        <div class="row g-4">
            <div class="col-lg-4">
                <div class="card border-0 shadow-sm"><div class="card-body">
                    <h1 class="h4 mb-3">Новая группа</h1>
                    <form method="post">
                        <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                        <input type="hidden" name="action" value="create_group">
                        <input type="hidden" name="redirect_page" value="groups">
                        <label class="form-label">Название</label>
                        <input class="form-control mb-3" name="name" placeholder="Например, ИС-21" required>
                        <button class="btn btn-primary">Создать группу</button>
                    </form>
                </div></div>
            </div>
            <div class="col-lg-8">
                <div class="card border-0 shadow-sm">
                    <div class="card-header bg-white py-3"><strong>Учебные группы</strong></div>
                    <div class="table-responsive"><table class="table mb-0">
                        <thead><tr><th>Название</th><th>Студентов</th><th>Действия</th></tr></thead>
                        <tbody>
                        <?php foreach ($groups as $group): ?>
                            <tr>
                                <td>
                                    <form class="d-flex gap-2" method="post">
                                        <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                                        <input type="hidden" name="action" value="rename_group">
                                        <input type="hidden" name="redirect_page" value="groups">
                                        <input type="hidden" name="id" value="<?= (int) $group['id'] ?>">
                                        <input class="form-control form-control-sm" name="name" value="<?= h($group['name']) ?>" required>
                                        <button class="btn btn-sm btn-outline-primary">Сохранить</button>
                                    </form>
                                </td>
                                <td><?= (int) $group['student_count'] ?></td>
                                <td>
                                    <form method="post" onsubmit="return confirm('Удалить пустую группу?')">
                                        <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                                        <input type="hidden" name="action" value="delete_group">
                                        <input type="hidden" name="redirect_page" value="groups">
                                        <input type="hidden" name="id" value="<?= (int) $group['id'] ?>">
                                        <button class="btn btn-sm btn-outline-danger" <?= (int) $group['student_count'] > 0 ? 'disabled' : '' ?>>Удалить</button>
                                    </form>
                                </td>
                            </tr>
                        <?php endforeach; ?>
                        <?php if (!$groups): ?><tr><td colspan="3" class="text-center text-secondary py-5">Создайте первую группу</td></tr><?php endif; ?>
                        </tbody>
                    </table></div>
                </div>
            </div>
        </div>

    <?php elseif ($page === 'students'): ?>
        <div class="card border-0 shadow-sm mb-4"><div class="card-body">
            <h1 class="h4 mb-3">Добавить студента</h1>
            <?php if (!$groupOptions): ?>
                <div class="alert alert-warning mb-0">Сначала создайте хотя бы одну группу.</div>
            <?php else: ?>
                <form class="row g-3 align-items-end" method="post">
                    <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                    <input type="hidden" name="action" value="create_student">
                    <input type="hidden" name="redirect_page" value="students">
                    <div class="col-md-3"><label class="form-label">Группа</label><select class="form-select" name="group_id" required><option value="">Выберите</option><?php foreach ($groupOptions as $group): ?><option value="<?= (int) $group['id'] ?>"><?= h($group['name']) ?></option><?php endforeach; ?></select></div>
                    <div class="col-md-3"><label class="form-label">Фамилия</label><input class="form-control" name="last_name" required></div>
                    <div class="col-md-3"><label class="form-label">Имя</label><input class="form-control" name="first_name" required></div>
                    <div class="col-md-3"><button class="btn btn-primary w-100">Добавить и создать код</button></div>
                </form>
            <?php endif; ?>
        </div></div>

        <div class="card border-0 shadow-sm">
            <div class="card-header bg-white py-3"><strong>Студенты</strong></div>
            <div class="table-responsive"><table class="table table-hover mb-0">
                <thead><tr><th>Группа</th><th>Фамилия</th><th>Имя</th><th>Код</th><th>Статус</th><th>Записей</th><th>Действия</th></tr></thead>
                <tbody>
                <?php foreach ($students as $student): ?>
                    <?php $formId = 'student-' . (int) $student['id']; ?>
                    <tr>
                        <td>
                            <form id="<?= h($formId) ?>" method="post">
                                <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                                <input type="hidden" name="action" value="update_student">
                                <input type="hidden" name="redirect_page" value="students">
                                <input type="hidden" name="id" value="<?= (int) $student['id'] ?>">
                            </form>
                            <select class="form-select form-select-sm" name="group_id" form="<?= h($formId) ?>">
                                <?php foreach ($groupOptions as $group): ?>
                                    <option value="<?= (int) $group['id'] ?>" <?= (int) $group['id'] === (int) $student['group_id'] ? 'selected' : '' ?>><?= h($group['name']) ?></option>
                                <?php endforeach; ?>
                            </select>
                        </td>
                        <td><input class="form-control form-control-sm" name="last_name" form="<?= h($formId) ?>" value="<?= h($student['last_name']) ?>" required></td>
                        <td><input class="form-control form-control-sm" name="first_name" form="<?= h($formId) ?>" value="<?= h($student['first_name']) ?>" required></td>
                        <td><span class="code"><?= h($student['code']) ?></span></td>
                        <td>
                            <select class="form-select form-select-sm" name="active" form="<?= h($formId) ?>">
                                <option value="1" <?= (int) $student['active'] === 1 ? 'selected' : '' ?>>Активен</option>
                                <option value="0" <?= (int) $student['active'] === 0 ? 'selected' : '' ?>>Отключён</option>
                            </select>
                        </td>
                        <td><a href="?page=recordings&student_id=<?= (int) $student['id'] ?>"><?= (int) $student['recording_count'] ?></a></td>
                        <td>
                            <div class="d-flex flex-wrap gap-2">
                                <button class="btn btn-sm btn-outline-primary" type="submit" form="<?= h($formId) ?>">Сохранить</button>
                                <form method="post" class="d-inline" onsubmit="return confirm('Создать новый код? Старые авторизации перестанут работать.')">
                                    <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                                    <input type="hidden" name="action" value="regenerate_code">
                                    <input type="hidden" name="redirect_page" value="students">
                                    <input type="hidden" name="id" value="<?= (int) $student['id'] ?>">
                                    <button class="btn btn-sm btn-outline-warning">Новый код</button>
                                </form>
                            </div>
                        </td>
                    </tr>
                <?php endforeach; ?>
                <?php if (!$students): ?><tr><td colspan="7" class="text-center text-secondary py-5">Студентов пока нет</td></tr><?php endif; ?>
                </tbody>
            </table></div>
        </div>

    <?php elseif ($page === 'recordings'): ?>
        <div class="card border-0 shadow-sm mb-4"><div class="card-body">
            <h1 class="h4 mb-3">Архив видеозаписей</h1>
            <form class="row g-3 align-items-end" method="get">
                <input type="hidden" name="page" value="recordings">
                <div class="col-md-3"><label class="form-label">Группа</label><select class="form-select" name="group_id"><option value="0">Все группы</option><?php foreach ($groupOptions as $group): ?><option value="<?= (int) $group['id'] ?>" <?= $selectedGroup === (int) $group['id'] ? 'selected' : '' ?>><?= h($group['name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-3"><label class="form-label">Студент</label><select class="form-select" name="student_id"><option value="0">Все студенты</option><?php foreach ($recordingStudents as $student): ?><option value="<?= (int) $student['id'] ?>" <?= $selectedStudent === (int) $student['id'] ? 'selected' : '' ?>><?= h($student['group_name'] . ' — ' . $student['last_name'] . ' ' . $student['first_name']) ?></option><?php endforeach; ?></select></div>
                <div class="col-md-2"><label class="form-label">С даты</label><input class="form-control" type="date" name="date_from" value="<?= h($dateFrom) ?>"></div>
                <div class="col-md-2"><label class="form-label">По дату</label><input class="form-control" type="date" name="date_to" value="<?= h($dateTo) ?>"></div>
                <div class="col-md-2"><button class="btn btn-primary w-100">Применить</button></div>
            </form>
        </div></div>

        <div class="row g-4">
            <?php foreach ($recordings as $record): ?>
                <div class="col-12 col-xl-6">
                    <article class="card recording-card border-0 shadow-sm h-100">
                        <video controls preload="metadata" src="stream.php?id=<?= (int) $record['id'] ?>"></video>
                        <div class="card-body">
                            <div class="d-flex justify-content-between gap-3">
                                <div><h2 class="h5 mb-1"><?= h($record['last_name'] . ' ' . $record['first_name']) ?></h2><div class="text-secondary"><?= h($record['group_name']) ?> · код <?= h($record['code']) ?></div></div>
                                <span class="badge text-bg-light align-self-start"><?= h(format_bytes((int) $record['file_size'])) ?></span>
                            </div>
                            <dl class="row small mt-3 mb-0">
                                <dt class="col-sm-4">Начало</dt><dd class="col-sm-8"><?= h($record['started_at'] ?: '—') ?></dd>
                                <dt class="col-sm-4">Окончание</dt><dd class="col-sm-8"><?= h($record['ended_at'] ?: '—') ?></dd>
                                <dt class="col-sm-4">Получено сервером</dt><dd class="col-sm-8"><?= h($record['created_at']) ?></dd>
                                <dt class="col-sm-4">Компьютер</dt><dd class="col-sm-8"><?= h($record['computer_name']) ?></dd>
                            </dl>
                        </div>
                        <div class="card-footer bg-white d-flex justify-content-between">
                            <a class="btn btn-sm btn-outline-primary" href="stream.php?id=<?= (int) $record['id'] ?>&download=1">Скачать MP4</a>
                            <form method="post" onsubmit="return confirm('Удалить видео без возможности восстановления?')">
                                <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                                <input type="hidden" name="action" value="delete_recording">
                                <input type="hidden" name="redirect_page" value="recordings">
                                <input type="hidden" name="id" value="<?= (int) $record['id'] ?>">
                                <button class="btn btn-sm btn-outline-danger">Удалить</button>
                            </form>
                        </div>
                    </article>
                </div>
            <?php endforeach; ?>
            <?php if (!$recordings): ?><div class="col-12"><div class="card border-0 shadow-sm"><div class="card-body text-center text-secondary py-5">По выбранным условиям записей нет</div></div></div><?php endif; ?>
        </div>
    <?php endif; ?>
</main>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
