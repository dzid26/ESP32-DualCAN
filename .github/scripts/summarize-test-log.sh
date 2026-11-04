#!/usr/bin/env bash
# Render a PlatformIO test log as a GitHub Actions job summary.
#
# Usage: summarize-test-log.sh <log-file>
#
# Writes markdown to $GITHUB_STEP_SUMMARY when set, otherwise to stdout.
# Runs even when the test step failed, so a red run still explains itself.

set -euo pipefail

log="${1:?usage: summarize-test-log.sh <log-file>}"
summary="${GITHUB_STEP_SUMMARY:-/dev/stdout}"

if [ ! -f "$log" ]; then
    echo "> No test log was produced." >> "$summary"
    exit 0
fi

# PlatformIO colourises on a TTY; strip ANSI so the summary stays readable.
clean="$(mktemp)"
trap 'rm -f "$clean"' EXIT
sed -E 's/\x1B\[[0-9;]*[mK]//g' "$log" > "$clean"

{
    echo "## Host unit tests"
    echo

    aggregate="$(grep -E '[0-9]+ test cases:' "$clean" | tail -n 1 || true)"
    if [ -n "$aggregate" ]; then
        echo '```text'
        echo "$aggregate"
        echo '```'
        echo
    fi

    if grep -qE '^=+ SUMMARY' "$clean"; then
        echo '```text'
        awk '/^=+ SUMMARY/ { show = 1 } show && !/^=+ [0-9]+ test cases:/' "$clean" \
            | sed 's/[[:space:]]*$//'
        echo '```'
    fi

    if grep -qE '\[(FAILED|ERRORED)\]' "$clean"; then
        echo
        echo '### Failed cases'
        echo
        echo '```text'
        grep -E '\[(FAILED|ERRORED)\]' "$clean" | sed 's/[[:space:]]*$//'
        echo '```'
    fi
} >> "$summary"
