#!/bin/sh
# Starts the DebugVision GUI on a virtual X display with a small log file and a fresh settings file, and fails
# when it exits (crashes) within 5 seconds instead of still running.
# usage: ci/gui-smoke-test.sh <path to debugvision> [settings.json]
set -u
exe=$1
work=$(mktemp -d)
printf 'smoke test line 1\nsmoke test line 2\n' > "$work/smoke.log"
if [ $# -ge 2 ]; then
    cp "$2" "$work/settings.json"
fi

xvfb-run -a timeout 5 "$exe" --settings "$work/settings.json" "$work/smoke.log" < /dev/null
rc=$?
rm -rf "$work"
if [ $rc -ne 124 ]; then
    echo "::error::debugvision exited with code $rc within 5 seconds (expected it to still be running)"
    exit 1
fi
echo "debugvision was still running after 5 seconds"
