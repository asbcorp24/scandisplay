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

    // Открытая страница server/public.php. Для отключения задайте SCANDISPLAY_PUBLIC_VIEW=0.
    'public_view_enabled' => filter_var(
        getenv('SCANDISPLAY_PUBLIC_VIEW') === false ? '1' : getenv('SCANDISPLAY_PUBLIC_VIEW'),
        FILTER_VALIDATE_BOOLEAN
    ),

    // Автоматическая выдача ffmpeg.exe клиентам.
    // Файл по умолчанию: server/storage/ffmpeg/ffmpeg.exe.
    'ffmpeg_download_enabled' => filter_var(
        getenv('SCANDISPLAY_FFMPEG_DOWNLOAD') === false ? '1' : getenv('SCANDISPLAY_FFMPEG_DOWNLOAD'),
        FILTER_VALIDATE_BOOLEAN
    ),
    'ffmpeg_file' => getenv('SCANDISPLAY_FFMPEG_FILE') ?: $root . '/storage/ffmpeg/ffmpeg.exe',
];
