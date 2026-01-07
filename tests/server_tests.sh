#!/usr/bin/env bash
set -euo pipefail

PORT=8080
HOST="http://127.0.0.1:${PORT}"
AUTH="Basic YWRtaW46cGFzc3dvcmQ="
SERVER_BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/server"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

assert_eq() {
    local got="$1"
    local expected="$2"
    local msg="$3"
    if [[ "${got}" != "${expected}" ]]; then
        echo "FAIL: ${msg}: expected '${expected}', got '${got}'"
        exit 1
    fi
}

assert_grep() {
    local pattern="$1"
    local file="$2"
    local msg="$3"
    if ! grep -q "${pattern}" "${file}"; then
        echo "FAIL: ${msg}: pattern '${pattern}' not found"
        exit 1
    fi
}

start_server() {
    pkill -f "/dev_projects/rest-api-c/server" 2>/dev/null || true
    "${SERVER_BIN}" >/tmp/server_test.log 2>&1 &
    SERVER_PID=$!
    sleep 0.2
}

status_from_file() {
    awk 'NR==1 {print $2}' "$1"
}

send_raw_request() {
    local payload="$1"
    local outfile
    outfile=$(mktemp)
    printf "%s" "${payload}" | nc 127.0.0.1 "${PORT}" >"${outfile}" || true
    echo "${outfile}"
}

curl_json() {
    local url="$1"
    shift
    local body_file
    body_file=$(mktemp)
    local status
    status=$(curl -s -o "${body_file}" -w "%{http_code}" "$@" "${url}")
    echo "${status}" "${body_file}"
}

main() {
    start_server

    # 401 без авторизации
    read -r status body < <(curl_json "${HOST}/health")
    assert_eq "${status}" "401" "GET /health без авторизации"
    assert_grep "unauthorized" "${body}" "тело 401"

    # 200 GET с авторизацией
    read -r status body < <(curl_json "${HOST}/test" -H "Authorization: ${AUTH}")
    assert_eq "${status}" "200" "GET /test c авторизацией"
    assert_grep '"message":"ok"' "${body}" "тело GET"

    # 200 POST c телом и длиной
    payload='{"hello":"world"}'
    read -r status body < <(curl_json "${HOST}/echo" -H "Authorization: ${AUTH}" -X POST -d "${payload}")
    assert_eq "${status}" "200" "POST /echo c авторизацией"
    assert_grep '"body_len": 17' "${body}" "body_len в ответе"

    # 405 неподдерживаемый метод
    read -r status body < <(curl_json "${HOST}/any" -H "Authorization: ${AUTH}" -X HEAD)
    assert_eq "${status}" "405" "HEAD /any -> 405"
    assert_grep "method not allowed" "${body}" "тело 405"

    # 401 при лишнем суффиксе в токене (префикс больше не принимается)
    read -r status body < <(curl_json "${HOST}/test" -H "Authorization: ${AUTH}EXTRA")
    assert_eq "${status}" "401" "GET /test с неверным токеном (префикс+суффикс)"

    # 400: нет разделителя заголовков и тела
    raw_file=$(send_raw_request $'GET /bad HTTP/1.1\r\nHost: x\r\n') # без \r\n\r\n
    raw_status=$(status_from_file "${raw_file}")
    assert_eq "${raw_status}" "400" "Отсутствует пустая строка после заголовков"

    # 400: нечисловой Content-Length
    raw_file=$(send_raw_request $'POST /badlen HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\npayload')
    raw_status=$(status_from_file "${raw_file}")
    assert_eq "${raw_status}" "400" "Нечисловой Content-Length"

    # 400: слишком большой Content-Length
    raw_file=$(send_raw_request $'POST /toolarge HTTP/1.1\r\nHost: x\r\nContent-Length: 200000\r\n\r\n')
    raw_status=$(status_from_file "${raw_file}")
    assert_eq "${raw_status}" "400" "Content-Length больше REQ_BUF"

    # 400: тело короче заявленного Content-Length
    raw_file=$(send_raw_request $'POST /short HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\n12345')
    raw_status=$(status_from_file "${raw_file}")
    assert_eq "${raw_status}" "400" "Тело короче указанного Content-Length"

    echo "OK: все тесты пройдены"
}

main "$@"

