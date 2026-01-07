# HTTP server (pure C) — инструкция по запуску

## Сборка
```bash
cd /Users/madmarchello/dev_projects/rest-api-c
make
```

## Запуск
```bash
./server
```
Сервер слушает `http://127.0.0.1:8080`. Basic auth: `admin:password`.

## Примеры запросов
Без авторизации (ожидается 401):
```bash
curl -i http://127.0.0.1:8080/health
```

GET с авторизацией:
```bash
curl -i -H "Authorization: Basic YWRtaW46cGFzc3dvcmQ=" \
  http://127.0.0.1:8080/test
```

POST с авторизацией и телом:
```bash
curl -i -X POST -H "Authorization: Basic YWRtaW46cGFzc3dvcmQ=" \
  -d '{"hello":"world"}' http://127.0.0.1:8080/echo
```

## Остановка
- `Ctrl+C` в окне сервера, или
- из другого окна: `pkill -f dev_projects/rest-api-c/server`

## Используемые библиотеки (все стандартные, без сторонних)
- `arpa/inet.h` — адреса и преобразование порядков байт.
- `errno.h` — коды ошибок системных вызовов.
- `netinet/in.h` — структуры/константы для IPv4 сокетов.
- `stdbool.h` — булевый тип.
- `stdio.h` — ввод/вывод (printf/snprintf).
- `stdlib.h` — утилиты (exit, atoi).
- `string.h` — строки/память (strlen, strncpy, strchr, strstr, strncmp, strcasecmp, strtok_r).
- `sys/socket.h` — сокеты (socket, bind, listen, accept, send, recv, setsockopt).
- `unistd.h` — POSIX функции (close).

