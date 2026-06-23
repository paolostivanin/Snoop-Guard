set -eu

daemon=$1
ctl=$2
root=$(mktemp -d)
pid=

cleanup() {
    if [ -n "$pid" ]; then
        kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$root"
}
trap cleanup EXIT INT TERM

mkdir -p "$root/config" "$root/state"
config="$root/config/snoop-guard.ini"
printf '[server]\ncheck_interval=2\nnotification_timeout=1\n' > "$config"

XDG_CONFIG_HOME="$root/config" XDG_STATE_HOME="$root/state" \
    "$daemon" --config "$config" &
pid=$!
sleep 1

"$ctl" status
"$ctl" --json status | grep -q '"webcam_active"'
"$ctl" --json health > "$root/health.json" || {
    rc=$?
    [ "$rc" -eq 2 ] || [ "$rc" -eq 3 ]
}
grep -q '"webcam_health"' "$root/health.json"
"$ctl" recent 20

set +e
XDG_CONFIG_HOME="$root/config" XDG_STATE_HOME="$root/state" \
    timeout 5 "$daemon" --config "$config"
second_rc=$?
set -e
if [ "$second_rc" -eq 0 ]; then
    echo "second daemon unexpectedly acquired the D-Bus name" >&2
    exit 1
fi
if [ "$second_rc" -ne 1 ]; then
    echo "second daemon exited with unexpected status $second_rc" >&2
    exit 1
fi

printf '[server]\ncheck_interval=0\n' > "$config"
if "$ctl" reload; then
    echo "malformed reload unexpectedly succeeded" >&2
    exit 1
fi
"$ctl" status

kill -TERM "$pid"
wait "$pid"
pid=
