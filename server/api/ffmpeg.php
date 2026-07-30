<?php

declare(strict_types=1);

require dirname(__DIR__) . '/bootstrap.php';

if (!in_array($_SERVER['REQUEST_METHOD'] ?? 'GET', ['GET', 'HEAD'], true)) {
    json_response(['ok' => false, 'message' => 'Метод не поддерживается.'], 405);
}

if (!(bool) config('ffmpeg_download_enabled')) {
    json_response(['ok' => false, 'message' => 'Автоматическая загрузка FFmpeg отключена.'], 403);
}

$file = (string) config('ffmpeg_file');
if ($file === '' || !is_file($file) || !is_readable($file)) {
    json_response([
        'ok' => false,
        'message' => 'ffmpeg.exe не размещён на сервере.',
    ], 404);
}

$size = filesize($file);
if ($size === false || $size < 1048576) {
    json_response(['ok' => false, 'message' => 'Файл ffmpeg.exe на сервере повреждён или слишком мал.'], 500);
}

$handle = fopen($file, 'rb');
if ($handle === false) {
    json_response(['ok' => false, 'message' => 'Не удалось открыть ffmpeg.exe.'], 500);
}

$signature = fread($handle, 2);
rewind($handle);
if ($signature !== "MZ") {
    fclose($handle);
    json_response(['ok' => false, 'message' => 'На сервере находится некорректный Windows EXE.'], 500);
}

$hash = hash_file('sha256', $file) ?: '';
header('Content-Type: application/vnd.microsoft.portable-executable');
header('Content-Disposition: attachment; filename="ffmpeg.exe"');
header('Content-Length: ' . $size);
header('Cache-Control: private, max-age=3600');
header('X-Content-Type-Options: nosniff');
if ($hash !== '') {
    header('ETag: "' . $hash . '"');
    header('X-Content-SHA256: ' . $hash);
}

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'HEAD') {
    fclose($handle);
    exit;
}

while (!feof($handle)) {
    $chunk = fread($handle, 1024 * 1024);
    if ($chunk === false) break;
    echo $chunk;
    if (function_exists('fastcgi_finish_request')) {
        // Не вызываем fastcgi_finish_request: поток должен быть передан полностью.
    }
    flush();
}
fclose($handle);
