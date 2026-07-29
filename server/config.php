<?php

declare(strict_types=1);

$root = __DIR__;

return [
    // Для MySQL задайте переменные окружения, например:
    // SCANDISPLAY_DSN="mysql:host=127.0.0.1;dbname=scandisplay;charset=utf8mb4"
    'db_dsn' => getenv('SCANDISPLAY_DSN') ?: 'sqlite:' . $root . '/storage/database.sqlite',
    'db_user' => getenv('SCANDISPLAY_DB_USER') ?: null,
    'db_password' => getenv('SCANDISPLAY_DB_PASSWORD') ?: null,

    'timezone' => getenv('SCANDISPLAY_TIMEZONE') ?: 'Europe/Moscow',
    'upload_dir' => getenv('SCANDISPLAY_UPLOAD_DIR') ?: $root . '/storage/videos',
    'max_upload_bytes' => (int) (getenv('SCANDISPLAY_MAX_UPLOAD_BYTES') ?: 2147483648),
    'student_session_days' => (int) (getenv('SCANDISPLAY_SESSION_DAYS') ?: 30),
];
