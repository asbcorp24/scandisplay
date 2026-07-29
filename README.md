# ScanDisplay

ScanDisplay — система фиксации экрана учебного компьютера и централизованного хранения видеозаписей по группам и студентам.

Система состоит из двух частей:

- **Windows-клиент на C++** — работает в трее, делает снимок всего экрана раз в секунду, добавляет в кадр группу, ФИО и текущее время, создаёт MP4 и отправляет его на сервер;
- **PHP-админка** — позволяет создавать группы и студентов, выдавать цифровые коды и просматривать видеозаписи каждого студента.

---

# Быстрый запуск на Windows через XAMPP

Этот вариант подходит для первого тестирования системы на одном компьютере или в локальной сети.

## 1. Что нужно установить

На компьютере должны быть установлены:

1. **Git**.
2. **XAMPP** с PHP 8.1 или новее.
3. **Visual Studio 2022** с компонентом:

```text
Desktop development with C++
```

4. **CMake 3.20 или новее**.
5. **FFmpeg** с поддержкой кодировщика `libx264`.

Рекомендуемая папка FFmpeg:

```text
C:\ffmpeg\bin\ffmpeg.exe
```

Проверить FFmpeg можно в PowerShell:

```powershell
C:\ffmpeg\bin\ffmpeg.exe -version
C:\ffmpeg\bin\ffmpeg.exe -encoders | findstr libx264
```

В результате второй команды должна присутствовать строка с `libx264`.

---

## 2. Скачать проект

Откройте PowerShell или командную строку:

```powershell
cd C:\xampp\htdocs
git clone https://github.com/asbcorp24/scandisplay.git
cd scandisplay
```

Пока изменения находятся в отдельной ветке, переключитесь на неё:

```powershell
git checkout feature/initial-mvp
```

После слияния pull request с веткой `main` эта команда не потребуется.

В результате проект должен находиться здесь:

```text
C:\xampp\htdocs\scandisplay
```

А серверная часть здесь:

```text
C:\xampp\htdocs\scandisplay\server
```

---

## 3. Настроить PHP для загрузки видео

Откройте файл:

```text
C:\xampp\php\php.ini
```

Найдите и измените параметры:

```ini
upload_max_filesize = 2048M
post_max_size = 2050M
max_execution_time = 0
max_input_time = 0
memory_limit = 256M
```

Убедитесь, что включены расширения:

```ini
extension=pdo_sqlite
extension=mbstring
extension=fileinfo
```

Если перед названием расширения стоит точка с запятой, удалите её:

```ini
;extension=pdo_sqlite
```

должно стать:

```ini
extension=pdo_sqlite
```

После изменения `php.ini` обязательно перезапустите Apache в панели XAMPP.

---

## 4. Запустить Apache

1. Откройте **XAMPP Control Panel**.
2. Нажмите **Start** напротив Apache.
3. Убедитесь, что строка Apache стала зелёной.

Проверьте сервер в браузере:

```text
http://127.0.0.1/scandisplay/server/install.php
```

Если страница открылась, PHP-сервер работает правильно.

---

## 5. Установить админку

Откройте:

```text
http://127.0.0.1/scandisplay/server/install.php
```

На странице установки:

1. Введите логин администратора — минимум 3 символа.
2. Введите пароль — минимум 8 символов.
3. Повторите пароль.
4. Нажмите **Установить систему**.

Установщик автоматически:

- создаст базу SQLite;
- создаст необходимые таблицы;
- создаст первую учётную запись администратора;
- подготовит каталог для видеозаписей.

База данных будет находиться здесь:

```text
C:\xampp\htdocs\scandisplay\server\storage\database.sqlite
```

Видео будут сохраняться здесь:

```text
C:\xampp\htdocs\scandisplay\server\storage\videos
```

После установки откроется страница входа:

```text
http://127.0.0.1/scandisplay/server/index.php
```

---

## 6. Создать группу и студента

После входа в админку:

1. Откройте раздел **Группы**.
2. Создайте группу, например:

```text
ИС-101
```

3. Откройте раздел **Студенты**.
4. Выберите группу.
5. Укажите фамилию и имя студента.
6. Сохраните студента.

После сохранения система автоматически создаст уникальный восьмизначный цифровой код, например:

```text
48271635
```

Этот код вводится в Windows-программе при авторизации.

Код можно перевыпустить кнопкой **Новый код**. После перевыпуска старые авторизации перестанут работать.

---

# Запуск Windows-клиента

## 7. Вариант 1 — скачать готовый EXE из GitHub Actions

1. Откройте репозиторий GitHub.
2. Перейдите в раздел **Actions**.
3. Откройте последний успешный запуск `ScanDisplay CI`.
4. Внизу страницы скачайте артефакт:

```text
ScanDisplay-Windows-x64
```

5. Распакуйте архив в отдельную папку, например:

```text
C:\ScanDisplay
```

В папке должен находиться:

```text
C:\ScanDisplay\ScanDisplay.exe
```

---

## 8. Вариант 2 — собрать клиент самостоятельно

Откройте **Developer PowerShell for VS 2022** или обычный PowerShell:

```powershell
cd C:\xampp\htdocs\scandisplay
cmake -S client -B client\build -A x64
cmake --build client\build --config Release
```

Готовая программа появится здесь:

```text
C:\xampp\htdocs\scandisplay\client\build\Release\ScanDisplay.exe
```

Можно создать отдельную рабочую папку:

```powershell
mkdir C:\ScanDisplay
copy client\build\Release\ScanDisplay.exe C:\ScanDisplay\ScanDisplay.exe
copy client\config.example.ini C:\ScanDisplay\config.ini
```

---

## 9. Создать config.ini

Рядом с `ScanDisplay.exe` обязательно должен находиться файл:

```text
config.ini
```

Можно скопировать пример:

```powershell
copy C:\xampp\htdocs\scandisplay\client\config.example.ini C:\ScanDisplay\config.ini
```

Для локального запуска содержимое должно выглядеть так:

```ini
[server]
base_url=http://127.0.0.1/scandisplay/server

[recording]
ffmpeg_path=C:\ffmpeg\bin\ffmpeg.exe
output_dir=%LOCALAPPDATA%\ScanDisplay\recordings
delete_after_upload=0

[client]
request_timeout_seconds=120
```

### Описание параметров

`base_url` — адрес каталога `server` без `/api/auth.php` и без `/index.php`:

```ini
base_url=http://127.0.0.1/scandisplay/server
```

`ffmpeg_path` — полный путь к `ffmpeg.exe`:

```ini
ffmpeg_path=C:\ffmpeg\bin\ffmpeg.exe
```

`output_dir` — каталог локального хранения MP4:

```ini
output_dir=%LOCALAPPDATA%\ScanDisplay\recordings
```

`delete_after_upload`:

- `0` — сохранять локальную копию MP4 после успешной отправки;
- `1` — удалять локальный MP4 после подтверждения сервера.

Для первых испытаний рекомендуется оставить:

```ini
delete_after_upload=0
```

---

## 10. Запустить программу

Запустите:

```text
C:\ScanDisplay\ScanDisplay.exe
```

Обычное окно не появится. Значок программы будет находиться в системном трее Windows рядом с часами.

Если значок не виден, нажмите стрелку **Показать скрытые значки**.

Щёлкните по значку правой кнопкой мыши. Доступны пункты:

- **Авторизация**;
- **Начать запись**;
- **Остановить запись**;
- **Выход**.

---

## 11. Авторизовать студента

1. Нажмите правой кнопкой по значку ScanDisplay.
2. Выберите **Авторизация**.
3. Введите восьмизначный код, созданный в админке.
4. Нажмите **Авторизоваться**.

При успешной авторизации программа получит с сервера:

- группу;
- фамилию;
- имя;
- временный токен доступа.

В меню трея рядом с пунктом авторизации будет показано ФИО студента.

---

## 12. Записать экран

1. Нажмите правой кнопкой по значку ScanDisplay.
2. Выберите **Начать запись**.
3. Работайте за компьютером.
4. Программа будет получать один новый кадр всего виртуального рабочего стола каждую секунду.
5. В верхней части каждого кадра будут записаны:
   - название группы;
   - фамилия и имя;
   - текущее локальное время компьютера.
6. Для завершения нажмите **Остановить запись**.

После остановки программа:

1. завершит формирование MP4 через FFmpeg;
2. отправит видео на сервер;
3. покажет уведомление об успешной отправке или ошибке;
4. при ошибке оставит файл локально.

Локальные записи находятся по адресу:

```text
%LOCALAPPDATA%\ScanDisplay\recordings
```

Для быстрого открытия вставьте этот путь в адресную строку Проводника:

```text
%LOCALAPPDATA%\ScanDisplay\recordings
```

---

## 13. Проверить видео в админке

Откройте:

```text
http://127.0.0.1/scandisplay/server/index.php
```

Войдите под администратором и перейдите в раздел **Видеозаписи**.

Там можно:

- отфильтровать записи по группе;
- выбрать конкретного студента;
- отфильтровать записи по датам;
- посмотреть видео в браузере;
- скачать MP4;
- удалить запись.

Если запись появилась и воспроизводится, полный цикл системы работает правильно.

---

# Запуск в локальной сети

Если PHP-сервер находится на отдельном компьютере, узнайте его IP-адрес:

```powershell
ipconfig
```

Например, сервер имеет адрес:

```text
192.168.1.50
```

Тогда админка будет открываться так:

```text
http://192.168.1.50/scandisplay/server/index.php
```

А на клиентских компьютерах в `config.ini` нужно указать:

```ini
[server]
base_url=http://192.168.1.50/scandisplay/server
```

Проверьте с клиентского компьютера, что в браузере открывается:

```text
http://192.168.1.50/scandisplay/server/index.php
```

Если страница не открывается:

1. разрешите Apache в брандмауэре Windows;
2. проверьте, что компьютеры находятся в одной сети;
3. проверьте IP-адрес сервера;
4. проверьте, что Apache запущен;
5. убедитесь, что используется правильный порт.

---

# Развёртывание на Linux-сервере

## 14. Требования

- PHP 8.1 или новее;
- `PDO`;
- `mbstring`;
- `fileinfo`;
- `pdo_sqlite` или `pdo_mysql`;
- Apache 2.4 или Nginx;
- HTTPS;
- право PHP на запись в каталог `server/storage`.

Пример установки пакетов для системы на базе Debian/Ubuntu:

```bash
sudo apt update
sudo apt install apache2 php php-sqlite3 php-mbstring php-curl php-xml libapache2-mod-php git
```

Скопируйте проект:

```bash
cd /var/www/html
sudo git clone https://github.com/asbcorp24/scandisplay.git
cd scandisplay
sudo git checkout feature/initial-mvp
```

Настройте права:

```bash
sudo chown -R www-data:www-data /var/www/html/scandisplay/server/storage
sudo chmod -R 775 /var/www/html/scandisplay/server/storage
```

Откройте установщик:

```text
https://ваш-домен/scandisplay/server/install.php
```

Для Nginx установите лимит размера запроса:

```nginx
client_max_body_size 2g;
```

Для PHP рекомендуется:

```ini
upload_max_filesize = 2048M
post_max_size = 2050M
max_execution_time = 0
max_input_time = 0
memory_limit = 256M
```

---

# Использование MySQL

По умолчанию используется SQLite. Для MySQL сначала создайте базу и пользователя:

```sql
CREATE DATABASE scandisplay CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'scandisplay'@'localhost' IDENTIFIED BY 'сложный_пароль';
GRANT ALL PRIVILEGES ON scandisplay.* TO 'scandisplay'@'localhost';
FLUSH PRIVILEGES;
```

Перед запуском PHP задайте переменные окружения:

```text
SCANDISPLAY_DSN=mysql:host=127.0.0.1;dbname=scandisplay;charset=utf8mb4
SCANDISPLAY_DB_USER=scandisplay
SCANDISPLAY_DB_PASSWORD=сложный_пароль
SCANDISPLAY_TIMEZONE=Europe/Moscow
```

Дополнительные параметры:

```text
SCANDISPLAY_UPLOAD_DIR=/srv/scandisplay-videos
SCANDISPLAY_MAX_UPLOAD_BYTES=2147483648
SCANDISPLAY_SESSION_DAYS=30
```

После настройки откройте `install.php`. Установщик создаст таблицы автоматически.

Рекомендуется хранить видеозаписи вне публичного каталога сайта:

```text
SCANDISPLAY_UPLOAD_DIR=/srv/scandisplay-videos
```

Каталог должен быть доступен пользователю веб-сервера:

```bash
sudo mkdir -p /srv/scandisplay-videos
sudo chown -R www-data:www-data /srv/scandisplay-videos
sudo chmod -R 775 /srv/scandisplay-videos
```

---

# Проверка API

## Авторизация студента

Адрес:

```text
POST /api/auth.php
```

Поля формы:

```text
code
computer_name
```

Пример для локального сервера:

```powershell
curl.exe -X POST "http://127.0.0.1/scandisplay/server/api/auth.php" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  -d "code=48271635&computer_name=TEST-PC"
```

При успешном запросе сервер вернёт JSON с данными студента и токеном.

## Загрузка видео

Адрес:

```text
POST /api/upload.php
```

Заголовок:

```text
Authorization: Bearer <token>
```

Поля `multipart/form-data`:

```text
started_at
ended_at
computer_name
video
```

Обычно вручную этот запрос выполнять не требуется — его отправляет Windows-клиент.

---

# Частые ошибки

## Программа сообщает, что отсутствует config.ini

Файл должен находиться в одной папке с `ScanDisplay.exe`:

```text
C:\ScanDisplay\ScanDisplay.exe
C:\ScanDisplay\config.ini
```

Название должно быть именно `config.ini`, а не:

```text
config.ini.txt
```

В Проводнике включите отображение расширений файлов.

---

## Ошибка «FFmpeg не найден»

Проверьте строку:

```ini
ffmpeg_path=C:\ffmpeg\bin\ffmpeg.exe
```

Затем убедитесь, что файл действительно существует:

```powershell
Test-Path C:\ffmpeg\bin\ffmpeg.exe
```

Команда должна вернуть:

```text
True
```

---

## Ошибка авторизации

Проверьте:

1. студент создан в админке;
2. студент имеет статус **Активен**;
3. введён актуальный цифровой код;
4. `base_url` указывает на правильный сервер;
5. адрес сервера открывается в браузере;
6. клиентский компьютер имеет доступ к серверу.

После перевыпуска кода старый код и старые сессии становятся недействительными.

---

## Видео не отправляется

Проверьте:

1. лимиты `upload_max_filesize` и `post_max_size`;
2. права записи в `server/storage/videos`;
3. свободное место на сервере;
4. работу Apache или Nginx;
5. доступность URL из `config.ini`;
6. наличие локального MP4 в `%LOCALAPPDATA%\ScanDisplay\recordings`;
7. журнал ошибок PHP и веб-сервера.

После изменения `php.ini` обязательно перезапустите Apache или PHP-FPM.

---

## В админке не воспроизводится видео

Проверьте:

1. файл существует в `server/storage/videos`;
2. PHP имеет право читать файл;
3. браузер поддерживает MP4/H.264;
4. FFmpeg собран с `libx264`;
5. запись была штатно остановлена через пункт **Остановить запись**.

---

## Значок программы не появился

1. Проверьте область скрытых значков Windows.
2. Убедитесь, что второй экземпляр программы не запущен.
3. Откройте Диспетчер задач и найдите `ScanDisplay.exe`.
4. Запустите программу из PowerShell, чтобы проверить наличие ошибок конфигурации.

---

# Структура проекта

```text
client/
  CMakeLists.txt
  config.example.ini
  src/
    main.cpp
    compat.hpp
server/
  api/
    auth.php
    upload.php
  storage/
    videos/
  bootstrap.php
  config.php
  index.php
  install.php
  stream.php
```

---

# Формат записи

Клиент получает один новый кадр в секунду. Для совместимости с браузерными проигрывателями FFmpeg создаёт стандартный видеопоток 25 кадров/с, повторяя текущий кадр до появления следующего снимка.

Фактическая частота наблюдения остаётся:

```text
1 кадр в секунду
```

Звук в текущей версии не записывается.

---

# Ограничения текущей версии

- максимальный размер одной отправки со стороны клиента — менее 4 ГБ;
- защищённый рабочий стол Windows, окно UAC и экран блокировки могут не попадать в запись;
- при аварийном завершении Windows MP4 может не успеть корректно закрыться;
- автоматическая повторная отправка неудачных загрузок пока не реализована;
- на одном компьютере одновременно используется один авторизованный студент;
- изменение разрешения или состава мониторов во время активной записи не рекомендуется;
- звук не записывается.

---

# Безопасность и правовые требования

При промышленном использовании:

- используйте HTTPS;
- ограничьте доступ к админке;
- установите сложный пароль администратора;
- вынесите каталог видео за пределы публичного каталога сайта;
- настройте резервное копирование базы и видеозаписей;
- установите сроки хранения и удаление старых записей;
- не храните пароли базы данных в Git;
- уведомляйте пользователей о записи экрана;
- получите необходимые согласия в соответствии с применимым законодательством и внутренними правилами организации.

---

# Проверки GitHub Actions

Workflow `.github/workflows/ci.yml` выполняет:

- проверку синтаксиса всех PHP-файлов через `php -l`;
- сборку C++-клиента MSVC x64;
- публикацию готового `ScanDisplay.exe` как артефакта `ScanDisplay-Windows-x64`.
