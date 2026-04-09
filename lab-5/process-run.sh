#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: ./process-run.sh <q1|q2|q3|q4|q5> [2|3] [remote-host]" >&2
    exit 1
fi

Q="$1"
PLOT_MODE="${2:-3}"
REMOTE_HOST="${3:-hpc_lab}"

./runs/copy-data.sh "$Q" "$REMOTE_HOST"

if [[ "$PLOT_MODE" == "2" ]]; then
    ./runs/generate-plot.sh "$Q"
else
    ./runs/generate-plot-3.sh "$Q"
fi
