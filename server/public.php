<?php

declare(strict_types=1);

require __DIR__ . '/bootstrap.php';

if (!is_installed()) {
    header('Location: install.php');
    exit;
}
if (!config('public_view_enabled')) {
    http_response_code(404);
    exit('Публичный просмотр отключён.');
}

ensure_recordings_title_column();
$pdo = db();

$selectedGroup = max(0, (int) ($_GET['group_id'] ?? 0));
$titleQuery = mb_substr(trim((string) ($_GET['title'] ?? '')), 0, 255);
$dateFrom = trim((string) ($_GET['date_from'] ?? ''));
$dateTo = trim((string) ($_GET['date_to'] ?? ''));
$page = max(1, (int) ($_GET['p'] ?? 1));
$perPage = 24;

$groups = $pdo->query('SELECT id, name FROM student_groups ORDER BY name')->fetchAll();

$where = [];
$params = [];
if ($selectedGroup > 0) {
    $where[] = 's.group_id = ?';
    $params[] = $selectedGroup;
}
if ($titleQuery !== '') {
    $where[] = 'r.title LIKE ?';
    $params[] = '%' . $titleQuery . '%';
}
if ($dateFrom !== '') {
    $where[] = 'r.created_at >= ?';
    $params[] = $dateFrom . ' 00:00:00';
}
if ($dateTo !== '') {
    $where[] = 'r.created_at <= ?';
    $params[] = $dateTo . ' 23:59:59';
}

$fromSql = ' FROM recordings r
             JOIN students s ON s.id = r.student_id
             JOIN student_groups g ON g.id = s.group_id';
$whereSql = $where ? ' WHERE ' . implode(' AND ', $where) : '';

$countStmt = $pdo->prepare('SELECT COUNT(*)' . $fromSql . $whereSql);
$countStmt->execute($params);
$total = (int) $countStmt->fetchColumn();
$totalPages = max(1, (int) ceil($total / $perPage));
$page = min($page, $totalPages);
$offset = ($page - 1) * $perPage;

$sql = 'SELECT r.id, r.title, r.file_size, r.started_at, r.ended_at, r.created_at,
               s.first_name, s.last_name, g.name AS group_name'
     . $fromSql . $whereSql
     . ' ORDER BY r.created_at DESC LIMIT ' . $perPage . ' OFFSET ' . $offset;
$stmt = $pdo->prepare($sql);
$stmt->execute($params);
$recordings = $stmt->fetchAll();

function public_query(array $changes = []): string
{
    $query = $_GET;
    foreach ($changes as $key => $value) {
        if ($value === null || $value === '' || $value === 0) {
            unset($query[$key]);
        } else {
            $query[$key] = $value;
        }
    }
    return '?' . http_build_query($query);
}
?>
<!doctype html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Публичные видеозаписи — ScanDisplay</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background:#f4f6fa; }
        .navbar-brand { font-weight:800; letter-spacing:-.03em; }
        .video-card { border:0; box-shadow:0 .35rem 1.4rem rgba(22,35,62,.08); overflow:hidden; }
        .video-card video { display:block; width:100%; aspect-ratio:16/9; background:#080808; object-fit:contain; }
        .speed-select { width:auto; min-width:105px; }
        .meta { color:#6c757d; font-size:.9rem; }
        .empty-state { min-height:240px; display:grid; place-items:center; }
    </style>
</head>
<body>
<nav class="navbar navbar-dark bg-dark shadow-sm">
    <div class="container-fluid px-lg-4">
        <a class="navbar-brand" href="public.php">ScanDisplay</a>
        <span class="navbar-text text-light-emphasis">Публичный архив видеозаписей</span>
        <a class="btn btn-outline-light btn-sm" href="index.php">Вход администратора</a>
    </div>
</nav>

<main class="container-fluid px-lg-4 py-4">
    <div class="d-flex flex-wrap justify-content-between align-items-end gap-3 mb-4">
        <div>
            <h1 class="h3 mb-1">Видеозаписи</h1>
            <div class="text-secondary">Найдено записей: <?= $total ?></div>
        </div>
    </div>

    <div class="card border-0 shadow-sm mb-4">
        <div class="card-body">
            <form class="row g-3 align-items-end" method="get">
                <div class="col-lg-4">
                    <label class="form-label" for="title">Название записи</label>
                    <input class="form-control" id="title" name="title" value="<?= h($titleQuery) ?>" placeholder="Название работы или задания">
                </div>
                <div class="col-lg-3">
                    <label class="form-label" for="group_id">Группа</label>
                    <select class="form-select" id="group_id" name="group_id">
                        <option value="0">Все группы</option>
                        <?php foreach ($groups as $group): ?>
                            <option value="<?= (int) $group['id'] ?>" <?= $selectedGroup === (int) $group['id'] ? 'selected' : '' ?>><?= h($group['name']) ?></option>
                        <?php endforeach; ?>
                    </select>
                </div>
                <div class="col-sm-6 col-lg-2">
                    <label class="form-label" for="date_from">С даты</label>
                    <input class="form-control" type="date" id="date_from" name="date_from" value="<?= h($dateFrom) ?>">
                </div>
                <div class="col-sm-6 col-lg-2">
                    <label class="form-label" for="date_to">По дату</label>
                    <input class="form-control" type="date" id="date_to" name="date_to" value="<?= h($dateTo) ?>">
                </div>
                <div class="col-lg-1 d-grid gap-2">
                    <button class="btn btn-primary">Найти</button>
                </div>
                <div class="col-12">
                    <a class="btn btn-sm btn-outline-secondary" href="public.php">Сбросить фильтры</a>
                </div>
            </form>
        </div>
    </div>

    <div class="row g-4">
        <?php foreach ($recordings as $record): ?>
            <div class="col-12 col-xl-6">
                <article class="card video-card h-100">
                    <video class="public-video" controls preload="metadata" playsinline src="public_stream.php?id=<?= (int) $record['id'] ?>"></video>
                    <div class="card-body">
                        <div class="d-flex flex-wrap justify-content-between gap-3 mb-3">
                            <div>
                                <h2 class="h5 mb-1"><?= h($record['title']) ?></h2>
                                <div class="fw-semibold"><?= h($record['last_name'] . ' ' . $record['first_name']) ?></div>
                                <div class="meta"><?= h($record['group_name']) ?></div>
                            </div>
                            <span class="badge text-bg-light align-self-start"><?= h(format_bytes((int) $record['file_size'])) ?></span>
                        </div>

                        <div class="d-flex flex-wrap align-items-center gap-2 mb-3">
                            <button class="btn btn-sm btn-outline-secondary seek-button" type="button" data-seconds="-10">−10 сек</button>
                            <button class="btn btn-sm btn-outline-secondary seek-button" type="button" data-seconds="10">+10 сек</button>
                            <label class="small text-secondary ms-sm-auto" for="speed-<?= (int) $record['id'] ?>">Скорость:</label>
                            <select class="form-select form-select-sm speed-select playback-speed" id="speed-<?= (int) $record['id'] ?>">
                                <option value="0.5">0,5×</option>
                                <option value="0.75">0,75×</option>
                                <option value="1" selected>1×</option>
                                <option value="1.25">1,25×</option>
                                <option value="1.5">1,5×</option>
                                <option value="2">2×</option>
                                <option value="3">3×</option>
                                <option value="4">4×</option>
                            </select>
                        </div>

                        <dl class="row small mb-0">
                            <dt class="col-sm-4">Начало</dt><dd class="col-sm-8"><?= h($record['started_at'] ?: '—') ?></dd>
                            <dt class="col-sm-4">Окончание</dt><dd class="col-sm-8"><?= h($record['ended_at'] ?: '—') ?></dd>
                            <dt class="col-sm-4">Опубликовано</dt><dd class="col-sm-8"><?= h($record['created_at']) ?></dd>
                        </dl>
                    </div>
                </article>
            </div>
        <?php endforeach; ?>

        <?php if (!$recordings): ?>
            <div class="col-12">
                <div class="card border-0 shadow-sm empty-state">
                    <div class="text-center text-secondary p-5">
                        <div class="h5">Записей не найдено</div>
                        <div>Измените условия поиска или сбросьте фильтры.</div>
                    </div>
                </div>
            </div>
        <?php endif; ?>
    </div>

    <?php if ($totalPages > 1): ?>
        <nav class="mt-4" aria-label="Страницы архива">
            <ul class="pagination justify-content-center flex-wrap">
                <li class="page-item <?= $page <= 1 ? 'disabled' : '' ?>"><a class="page-link" href="<?= h(public_query(['p' => $page - 1])) ?>">Назад</a></li>
                <?php for ($number = max(1, $page - 2); $number <= min($totalPages, $page + 2); ++$number): ?>
                    <li class="page-item <?= $number === $page ? 'active' : '' ?>"><a class="page-link" href="<?= h(public_query(['p' => $number])) ?>"><?= $number ?></a></li>
                <?php endfor; ?>
                <li class="page-item <?= $page >= $totalPages ? 'disabled' : '' ?>"><a class="page-link" href="<?= h(public_query(['p' => $page + 1])) ?>">Вперёд</a></li>
            </ul>
        </nav>
    <?php endif; ?>
</main>

<script>
document.querySelectorAll('.video-card').forEach((card) => {
    const video = card.querySelector('video');
    const speed = card.querySelector('.playback-speed');

    speed.addEventListener('change', () => {
        const rate = Number(speed.value) || 1;
        video.defaultPlaybackRate = rate;
        video.playbackRate = rate;
    });

    card.querySelectorAll('.seek-button').forEach((button) => {
        button.addEventListener('click', () => {
            const seconds = Number(button.dataset.seconds) || 0;
            const duration = Number.isFinite(video.duration) ? video.duration : Number.MAX_SAFE_INTEGER;
            video.currentTime = Math.max(0, Math.min(duration, video.currentTime + seconds));
        });
    });

    video.addEventListener('play', () => {
        document.querySelectorAll('.public-video').forEach((other) => {
            if (other !== video) other.pause();
        });
    });
});
</script>
</body>
</html>
