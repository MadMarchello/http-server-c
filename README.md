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

