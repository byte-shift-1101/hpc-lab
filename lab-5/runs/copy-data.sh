#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: ./runs/copy-data.sh <q1|q2|q3|q4|q5> [remote-host]" >&2
    exit 1
fi

Q="$1"
REMOTE_HOST="${2:-hpc_lab}"
REMOTE_BASE="~/lab-5/runs/$Q"
LOCAL_BASE="./runs/$Q"

mkdir -p "$LOCAL_BASE"

scp "$REMOTE_HOST:$REMOTE_BASE/length.txt" "$LOCAL_BASE/length.txt"
scp "$REMOTE_HOST:$REMOTE_BASE/nodes.txt" "$LOCAL_BASE/nodes.txt"
scp "$REMOTE_HOST:$REMOTE_BASE/processes.txt" "$LOCAL_BASE/processes.txt"

echo "Copied data for $Q from $REMOTE_HOST"
