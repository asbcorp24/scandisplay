<?php

declare(strict_types=1);

$config = require __DIR__ . '/config.php';
date_default_timezone_set($config['timezone']);

if (PHP_SAPI !== 'cli' && session_status() !== PHP_SESSION_ACTIVE) {
    session_name('scandisplay_admin');
    session_set_cookie_params([
        'httponly' => true,
        'secure' => !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off',
        'samesite' => 'Lax',
    ]);
    session_start();
}

function config(string $key): mixed
{
    global $config;
    return $config[$key] ?? null;
}

function db(): PDO
{
    static $pdo = null;
    if ($pdo instanceof PDO) {
        return $pdo;
    }

    $dsn = (string) config('db_dsn');
    if (str_starts_with($dsn, 'sqlite:')) {
        $path = substr($dsn, 7);
        $dir = dirname($path);
        if (!is_dir($dir)) {
            mkdir($dir, 0775, true);
        }
    }

    $pdo = new PDO($dsn, config('db_user'), config('db_password'), [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES => false,
    ]);
    if (str_starts_with($dsn, 'sqlite:')) {
        $pdo->exec('PRAGMA foreign_keys = ON');
    }
    return $pdo;
}

function is_installed(): bool
{
    try {
        $driver = db()->getAttribute(PDO::ATTR_DRIVER_NAME);
        if ($driver === 'sqlite') {
            $stmt = db()->query("SELECT name FROM sqlite_master WHERE type='table' AND name='admins'");
            return (bool) $stmt->fetchColumn();
        }
        db()->query('SELECT 1 FROM admins LIMIT 1');
        return true;
    } catch (Throwable) {
        return false;
    }
}

function h(?string $value): string
{
    return htmlspecialchars((string) $value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function json_response(array $payload, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($payload, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function bearer_token(): string
{
    $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if ($header === '' && function_exists('getallheaders')) {
        $headers = getallheaders();
        $header = $headers['Authorization'] ?? $headers['authorization'] ?? '';
    }
    return preg_match('/^Bearer\s+(.+)$/i', trim($header), $match) ? trim($match[1]) : '';
}

function require_admin(): void
{
    if (empty($_SESSION['admin_id'])) {
        header('Location: index.php');
        exit;
    }
}

function csrf_token(): string
{
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(32));
    }
    return $_SESSION['csrf'];
}

function require_csrf(): void
{
    $token = (string) ($_POST['csrf'] ?? '');
    if ($token === '' || !hash_equals((string) ($_SESSION['csrf'] ?? ''), $token)) {
        http_response_code(419);
        exit('Сессия формы истекла. Обновите страницу.');
    }
}

function random_token(int $bytes = 32): string
{
    return bin2hex(random_bytes($bytes));
}

function generate_student_code(PDO $pdo): string
{
    do {
        $code = (string) random_int(10000000, 99999999);
        $stmt = $pdo->prepare('SELECT COUNT(*) FROM students WHERE code = ?');
        $stmt->execute([$code]);
    } while ((int) $stmt->fetchColumn() > 0);
    return $code;
}

function format_bytes(int $bytes): string
{
    if ($bytes < 1024) return $bytes . ' Б';
    if ($bytes < 1024 * 1024) return number_format($bytes / 1024, 1, ',', ' ') . ' КБ';
    if ($bytes < 1024 * 1024 * 1024) return number_format($bytes / 1024 / 1024, 1, ',', ' ') . ' МБ';
    return number_format($bytes / 1024 / 1024 / 1024, 2, ',', ' ') . ' ГБ';
}

function flash(string $type, string $message): void
{
    $_SESSION['flash'] = ['type' => $type, 'message' => $message];
}

function take_flash(): ?array
{
    $flash = $_SESSION['flash'] ?? null;
    unset($_SESSION['flash']);
    return is_array($flash) ? $flash : null;
}
