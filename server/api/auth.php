<?php

declare(strict_types=1);

require dirname(__DIR__) . '/bootstrap.php';

if (!is_installed()) {
    json_response(['ok' => false, 'message' => 'Сервер не установлен.'], 503);
}
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_response(['ok' => false, 'message' => 'Метод не поддерживается.'], 405);
}

$code = trim((string) ($_POST['code'] ?? ''));
$computerName = trim((string) ($_POST['computer_name'] ?? 'unknown'));
if (!preg_match('/^\d{4,32}$/', $code)) {
    json_response(['ok' => false, 'message' => 'Некорректный цифровой код.'], 422);
}
$computerName = mb_substr($computerName !== '' ? $computerName : 'unknown', 0, 255);

$pdo = db();
$stmt = $pdo->prepare(
    'SELECT s.id, s.code, s.first_name, s.last_name, g.name AS group_name
     FROM students s
     JOIN student_groups g ON g.id = s.group_id
     WHERE s.code = ? AND s.active = 1
     LIMIT 1'
);
$stmt->execute([$code]);
$student = $stmt->fetch();

if (!$student) {
    usleep(350000);
    json_response(['ok' => false, 'message' => 'Студент с таким кодом не найден или отключён.'], 401);
}

$pdo->prepare('DELETE FROM student_sessions WHERE expires_at < ?')->execute([date('Y-m-d H:i:s')]);
$rawToken = random_token(32);
$tokenHash = hash('sha256', $rawToken);
$expiresAt = date('Y-m-d H:i:s', time() + ((int) config('student_session_days') * 86400));

$stmt = $pdo->prepare(
    'INSERT INTO student_sessions (student_id, token_hash, computer_name, expires_at, created_at)
     VALUES (?, ?, ?, ?, ?)'
);
$stmt->execute([(int) $student['id'], $tokenHash, $computerName, $expiresAt, date('Y-m-d H:i:s')]);

json_response([
    'ok' => true,
    'token' => $rawToken,
    'expires_at' => $expiresAt,
    'student' => [
        'id' => (int) $student['id'],
        'code' => $student['code'],
        'first_name' => $student['first_name'],
        'last_name' => $student['last_name'],
        'group_name' => $student['group_name'],
    ],
]);
