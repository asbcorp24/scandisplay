<?php

declare(strict_types=1);

require __DIR__ . '/bootstrap.php';
require_admin();
ensure_recordings_title_column();

$id = (int) ($_GET['id'] ?? 0);
$stmt = db()->prepare('SELECT id, title, file_path, original_name FROM recordings WHERE id = ?');
$stmt->execute([$id]);
$recording = $stmt->fetch();
if (!$recording) {
    http_response_code(404);
    exit('Запись не найдена.');
}

$root = realpath((string) config('upload_dir'));
$path = realpath(rtrim((string) config('upload_dir'), '/\\') . DIRECTORY_SEPARATOR . $recording['file_path']);
if ($root === false || $path === false || !str_starts_with($path, $root . DIRECTORY_SEPARATOR) || !is_file($path)) {
    http_response_code(404);
    exit('Файл видеозаписи отсутствует.');
}

$size = filesize($path);
if ($size === false || $size <= 0) {
    http_response_code(404);
    exit('Файл пуст или недоступен.');
}

$start = 0;
$end = $size - 1;
$status = 200;
$range = $_SERVER['HTTP_RANGE'] ?? '';

if ($range !== '' && preg_match('/bytes=(\d*)-(\d*)/i', $range, $match)) {
    if ($match[1] === '' && $match[2] !== '') {
        $suffixLength = min((int) $match[2], $size);
        $start = $size - $suffixLength;
    } else {
        $start = (int) $match[1];
        if ($match[2] !== '') $end = min((int) $match[2], $size - 1);
    }

    if ($start < 0 || $start > $end || $start >= $size) {
        header('Content-Range: bytes */' . $size);
        http_response_code(416);
        exit;
    }
    $status = 206;
}

$length = $end - $start + 1;
http_response_code($status);
header('Content-Type: video/mp4');
header('Accept-Ranges: bytes');
header('Content-Length: ' . $length);
header('Cache-Control: private, no-store');
if ($status === 206) {
    header("Content-Range: bytes {$start}-{$end}/{$size}");
}

$download = isset($_GET['download']);
$disposition = $download ? 'attachment' : 'inline';
$sourceName = $download ? ((string) $recording['title'] . '.mp4') : (string) $recording['original_name'];
$downloadName = preg_replace('/[^\pL\pN._-]+/u', '_', $sourceName) ?: 'recording.mp4';
header("Content-Disposition: {$disposition}; filename*=UTF-8''" . rawurlencode($downloadName));

$handle = fopen($path, 'rb');
if ($handle === false) {
    http_response_code(500);
    exit;
}
fseek($handle, $start);
$remaining = $length;
while ($remaining > 0 && !feof($handle)) {
    $chunk = fread($handle, (int) min(1024 * 1024, $remaining));
    if ($chunk === false || $chunk === '') break;
    echo $chunk;
    $remaining -= strlen($chunk);
    if (connection_aborted()) break;
    flush();
}
fclose($handle);
