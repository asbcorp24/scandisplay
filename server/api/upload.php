<?php

declare(strict_types=1);

require dirname(__DIR__) . '/bootstrap.php';

if (!is_installed()) {
    json_response(['ok' => false, 'message' => 'Сервер не установлен.'], 503);
}
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_response(['ok' => false, 'message' => 'Метод не поддерживается.'], 405);
}

$token = bearer_token();
if ($token === '') {
    json_response(['ok' => false, 'message' => 'Отсутствует токен авторизации.'], 401);
}

$pdo = db();
$stmt = $pdo->prepare(
    'SELECT ss.student_id, ss.computer_name AS authorized_computer, s.active
     FROM student_sessions ss
     JOIN students s ON s.id = ss.student_id
     WHERE ss.token_hash = ? AND ss.expires_at >= ?
     LIMIT 1'
);
$stmt->execute([hash('sha256', $token), date('Y-m-d H:i:s')]);
$session = $stmt->fetch();
if (!$session || !(int) $session['active']) {
    json_response(['ok' => false, 'message' => 'Авторизация истекла. Выполните её повторно.'], 401);
}

if (!isset($_FILES['video'])) {
    json_response([
        'ok' => false,
        'message' => 'Видео не получено. Проверьте upload_max_filesize и post_max_size в php.ini.',
    ], 422);
}

$file = $_FILES['video'];
if ((int) $file['error'] !== UPLOAD_ERR_OK) {
    $messages = [
        UPLOAD_ERR_INI_SIZE => 'Файл превышает upload_max_filesize в php.ini.',
        UPLOAD_ERR_FORM_SIZE => 'Файл превышает допустимый размер формы.',
        UPLOAD_ERR_PARTIAL => 'Видео загружено не полностью.',
        UPLOAD_ERR_NO_FILE => 'Видео не передано.',
        UPLOAD_ERR_NO_TMP_DIR => 'На сервере отсутствует временный каталог.',
        UPLOAD_ERR_CANT_WRITE => 'Сервер не смог записать временный файл.',
        UPLOAD_ERR_EXTENSION => 'Загрузка остановлена расширением PHP.',
    ];
    json_response(['ok' => false, 'message' => $messages[(int) $file['error']] ?? 'Ошибка загрузки файла.'], 422);
}

$fileSize = (int) $file['size'];
if ($fileSize <= 0 || $fileSize > (int) config('max_upload_bytes')) {
    json_response(['ok' => false, 'message' => 'Недопустимый размер видео.'], 413);
}

$mime = (new finfo(FILEINFO_MIME_TYPE))->file($file['tmp_name']) ?: '';
$allowedMime = ['video/mp4', 'application/mp4', 'application/octet-stream'];
if (!in_array($mime, $allowedMime, true)) {
    json_response(['ok' => false, 'message' => 'Разрешены только MP4-видеозаписи. Получен тип: ' . $mime], 415);
}

$normalizeDate = static function (string $value): ?string {
    $value = trim($value);
    if ($value === '') return null;
    $date = DateTimeImmutable::createFromFormat('!Y-m-d H:i:s', $value);
    $errors = DateTimeImmutable::getLastErrors();
    if (!$date || ($errors !== false && ($errors['warning_count'] > 0 || $errors['error_count'] > 0))) return null;
    return $date->format('Y-m-d H:i:s');
};

$startedAtRaw = (string) ($_POST['started_at'] ?? '');
$endedAtRaw = (string) ($_POST['ended_at'] ?? '');
$startedAt = $normalizeDate($startedAtRaw);
$endedAt = $normalizeDate($endedAtRaw);
if (($startedAtRaw !== '' && $startedAt === null) || ($endedAtRaw !== '' && $endedAt === null)) {
    json_response(['ok' => false, 'message' => 'Некорректная дата начала или окончания записи.'], 422);
}

$computerName = trim((string) ($_POST['computer_name'] ?? $session['authorized_computer']));
$computerName = mb_substr($computerName !== '' ? $computerName : 'unknown', 0, 255);
$studentId = (int) $session['student_id'];
$relativeDir = $studentId . DIRECTORY_SEPARATOR . date('Y') . DIRECTORY_SEPARATOR . date('m');
$absoluteDir = rtrim((string) config('upload_dir'), '/\\') . DIRECTORY_SEPARATOR . $relativeDir;
if (!is_dir($absoluteDir) && !mkdir($absoluteDir, 0775, true) && !is_dir($absoluteDir)) {
    json_response(['ok' => false, 'message' => 'Сервер не смог создать каталог хранения.'], 500);
}

$fileName = date('Ymd_His') . '_' . bin2hex(random_bytes(8)) . '.mp4';
$relativePath = $relativeDir . DIRECTORY_SEPARATOR . $fileName;
$absolutePath = $absoluteDir . DIRECTORY_SEPARATOR . $fileName;
if (!move_uploaded_file($file['tmp_name'], $absolutePath)) {
    json_response(['ok' => false, 'message' => 'Сервер не смог переместить загруженное видео.'], 500);
}

try {
    $stmt = $pdo->prepare(
        'INSERT INTO recordings (student_id, file_path, original_name, file_size, started_at, ended_at, computer_name, created_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([
        $studentId,
        $relativePath,
        mb_substr((string) ($file['name'] ?? 'recording.mp4'), 0, 255),
        filesize($absolutePath) ?: $fileSize,
        $startedAt,
        $endedAt,
        $computerName,
        date('Y-m-d H:i:s'),
    ]);
    $recordingId = (int) $pdo->lastInsertId();
} catch (Throwable $exception) {
    @unlink($absolutePath);
    json_response(['ok' => false, 'message' => 'Ошибка сохранения записи в базе данных.'], 500);
}

json_response([
    'ok' => true,
    'recording_id' => $recordingId,
    'size' => filesize($absolutePath) ?: $fileSize,
]);
