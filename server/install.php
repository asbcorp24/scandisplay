<?php

declare(strict_types=1);

require __DIR__ . '/bootstrap.php';

$error = '';
$installed = is_installed();

if ($_SERVER['REQUEST_METHOD'] === 'POST' && !$installed) {
    require_csrf();
    $username = trim((string) ($_POST['username'] ?? ''));
    $password = (string) ($_POST['password'] ?? '');
    $passwordConfirm = (string) ($_POST['password_confirm'] ?? '');

    if ($username === '' || mb_strlen($username) < 3) {
        $error = 'Логин должен содержать не менее трёх символов.';
    } elseif (mb_strlen($password) < 8) {
        $error = 'Пароль должен содержать не менее восьми символов.';
    } elseif ($password !== $passwordConfirm) {
        $error = 'Пароли не совпадают.';
    } else {
        try {
            $pdo = db();
            $driver = $pdo->getAttribute(PDO::ATTR_DRIVER_NAME);

            if ($driver === 'mysql') {
                $schema = [
                    "CREATE TABLE IF NOT EXISTS admins (
                        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                        username VARCHAR(100) NOT NULL UNIQUE,
                        password_hash VARCHAR(255) NOT NULL,
                        created_at DATETIME NOT NULL
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
                    "CREATE TABLE IF NOT EXISTS student_groups (
                        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                        name VARCHAR(255) NOT NULL UNIQUE,
                        created_at DATETIME NOT NULL
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
                    "CREATE TABLE IF NOT EXISTS students (
                        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                        group_id BIGINT UNSIGNED NOT NULL,
                        first_name VARCHAR(255) NOT NULL,
                        last_name VARCHAR(255) NOT NULL,
                        code VARCHAR(32) NOT NULL UNIQUE,
                        active TINYINT(1) NOT NULL DEFAULT 1,
                        created_at DATETIME NOT NULL,
                        CONSTRAINT fk_students_group FOREIGN KEY (group_id) REFERENCES student_groups(id) ON DELETE RESTRICT,
                        INDEX idx_students_group (group_id),
                        INDEX idx_students_name (last_name, first_name)
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
                    "CREATE TABLE IF NOT EXISTS student_sessions (
                        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                        student_id BIGINT UNSIGNED NOT NULL,
                        token_hash CHAR(64) NOT NULL UNIQUE,
                        computer_name VARCHAR(255) NOT NULL,
                        expires_at DATETIME NOT NULL,
                        created_at DATETIME NOT NULL,
                        CONSTRAINT fk_sessions_student FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE,
                        INDEX idx_sessions_student (student_id),
                        INDEX idx_sessions_expires (expires_at)
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
                    "CREATE TABLE IF NOT EXISTS recordings (
                        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                        student_id BIGINT UNSIGNED NOT NULL,
                        file_path VARCHAR(1024) NOT NULL,
                        original_name VARCHAR(255) NOT NULL,
                        file_size BIGINT UNSIGNED NOT NULL,
                        started_at DATETIME NULL,
                        ended_at DATETIME NULL,
                        computer_name VARCHAR(255) NOT NULL,
                        created_at DATETIME NOT NULL,
                        CONSTRAINT fk_recordings_student FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE,
                        INDEX idx_recordings_student (student_id),
                        INDEX idx_recordings_created (created_at)
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
                ];
            } else {
                $schema = [
                    "CREATE TABLE IF NOT EXISTS admins (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        username TEXT NOT NULL UNIQUE,
                        password_hash TEXT NOT NULL,
                        created_at TEXT NOT NULL
                    )",
                    "CREATE TABLE IF NOT EXISTS student_groups (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        name TEXT NOT NULL UNIQUE,
                        created_at TEXT NOT NULL
                    )",
                    "CREATE TABLE IF NOT EXISTS students (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        group_id INTEGER NOT NULL,
                        first_name TEXT NOT NULL,
                        last_name TEXT NOT NULL,
                        code TEXT NOT NULL UNIQUE,
                        active INTEGER NOT NULL DEFAULT 1,
                        created_at TEXT NOT NULL,
                        FOREIGN KEY (group_id) REFERENCES student_groups(id) ON DELETE RESTRICT
                    )",
                    "CREATE INDEX IF NOT EXISTS idx_students_group ON students(group_id)",
                    "CREATE INDEX IF NOT EXISTS idx_students_name ON students(last_name, first_name)",
                    "CREATE TABLE IF NOT EXISTS student_sessions (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        student_id INTEGER NOT NULL,
                        token_hash TEXT NOT NULL UNIQUE,
                        computer_name TEXT NOT NULL,
                        expires_at TEXT NOT NULL,
                        created_at TEXT NOT NULL,
                        FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE
                    )",
                    "CREATE INDEX IF NOT EXISTS idx_sessions_student ON student_sessions(student_id)",
                    "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON student_sessions(expires_at)",
                    "CREATE TABLE IF NOT EXISTS recordings (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        student_id INTEGER NOT NULL,
                        file_path TEXT NOT NULL,
                        original_name TEXT NOT NULL,
                        file_size INTEGER NOT NULL,
                        started_at TEXT NULL,
                        ended_at TEXT NULL,
                        computer_name TEXT NOT NULL,
                        created_at TEXT NOT NULL,
                        FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE
                    )",
                    "CREATE INDEX IF NOT EXISTS idx_recordings_student ON recordings(student_id)",
                    "CREATE INDEX IF NOT EXISTS idx_recordings_created ON recordings(created_at)",
                ];
            }

            foreach ($schema as $sql) {
                $pdo->exec($sql);
            }

            $count = (int) $pdo->query('SELECT COUNT(*) FROM admins')->fetchColumn();
            if ($count === 0) {
                $stmt = $pdo->prepare('INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)');
                $stmt->execute([$username, password_hash($password, PASSWORD_DEFAULT), date('Y-m-d H:i:s')]);
            }

            $uploadDir = (string) config('upload_dir');
            if (!is_dir($uploadDir) && !mkdir($uploadDir, 0775, true) && !is_dir($uploadDir)) {
                throw new RuntimeException('Не удалось создать каталог видео: ' . $uploadDir);
            }

            flash('success', 'Установка завершена. Войдите под созданным администратором.');
            header('Location: index.php');
            exit;
        } catch (Throwable $exception) {
            $error = 'Ошибка установки: ' . $exception->getMessage();
        }
    }
}
?>
<!doctype html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Установка ScanDisplay</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/css/bootstrap.min.css" rel="stylesheet">
</head>
<body class="bg-body-tertiary">
<main class="container py-5" style="max-width: 680px">
    <div class="card shadow-sm">
        <div class="card-body p-4 p-md-5">
            <h1 class="h3 mb-3">Установка ScanDisplay</h1>
            <?php if ($installed): ?>
                <div class="alert alert-success">Система уже установлена.</div>
                <a class="btn btn-primary" href="index.php">Перейти в админку</a>
            <?php else: ?>
                <p class="text-secondary">Будут созданы таблицы базы данных, каталог видео и первая учётная запись администратора.</p>
                <?php if ($error !== ''): ?>
                    <div class="alert alert-danger"><?= h($error) ?></div>
                <?php endif; ?>
                <form method="post" autocomplete="off">
                    <input type="hidden" name="csrf" value="<?= h(csrf_token()) ?>">
                    <div class="mb-3">
                        <label class="form-label" for="username">Логин администратора</label>
                        <input class="form-control" id="username" name="username" required minlength="3" value="<?= h($_POST['username'] ?? 'admin') ?>">
                    </div>
                    <div class="mb-3">
                        <label class="form-label" for="password">Пароль</label>
                        <input class="form-control" id="password" name="password" type="password" required minlength="8">
                    </div>
                    <div class="mb-4">
                        <label class="form-label" for="password_confirm">Повторите пароль</label>
                        <input class="form-control" id="password_confirm" name="password_confirm" type="password" required minlength="8">
                    </div>
                    <button class="btn btn-primary" type="submit">Установить систему</button>
                </form>
            <?php endif; ?>
        </div>
    </div>
</main>
</body>
</html>
