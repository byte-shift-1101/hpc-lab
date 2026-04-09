#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: ./runs/generate-plot-3.sh <q1|q2|q3|q4|q5>" >&2
    exit 1
fi

Q="$1"

python plotter3.py "runs/$Q/processes.txt" processes "runs/$Q/processes.png"
python plotter3.py "runs/$Q/nodes.txt" nodes "runs/$Q/nodes.png"
python plotter3.py "runs/$Q/length.txt" length "runs/$Q/length.png"

echo "Generated 3-series plots for $Q"
