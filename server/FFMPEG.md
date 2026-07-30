# FFmpeg для автоматической загрузки клиентом

Клиент ScanDisplay автоматически скачивает `ffmpeg.exe`, если файл отсутствует рядом с программой.

## Куда положить файл на сервере

Разместите 64-битную Windows-сборку FFmpeg по пути:

```text
server/storage/ffmpeg/ffmpeg.exe
```

Для XAMPP пример полного пути:

```text
C:\xampp\htdocs\scandisplay\server\storage\ffmpeg\ffmpeg.exe
```

Пример копирования:

```powershell
mkdir C:\xampp\htdocs\scandisplay\server\storage\ffmpeg
copy C:\ffmpeg\bin\ffmpeg.exe C:\xampp\htdocs\scandisplay\server\storage\ffmpeg\ffmpeg.exe
```

Клиент скачивает файл по адресу:

```text
<base_url>/api/ffmpeg.php
```

Например:

```text
http://127.0.0.1/scandisplay/server/api/ffmpeg.php
```

Каталог `storage/ffmpeg` закрыт от прямого просмотра через Apache. PHP-обработчик выдаёт только фиксированный файл `ffmpeg.exe`, проверяет его наличие, минимальный размер и сигнатуру Windows EXE `MZ`.

## Переменные окружения

Отключить выдачу файла:

```text
SCANDISPLAY_FFMPEG_DOWNLOAD=0
```

Указать другой путь к файлу:

```text
SCANDISPLAY_FFMPEG_FILE=C:\path\to\ffmpeg.exe
```

## Поведение клиента

Если локального `ffmpeg.exe` нет, клиент:

1. Загружает файл как `ffmpeg.exe.download`.
2. Проверяет, что размер не меньше 1 МБ.
3. Проверяет сигнатуру `MZ`.
4. Переименовывает файл в `ffmpeg.exe`.
5. Продолжает обычный запуск.

При обрыве загрузки временный файл удаляется.
