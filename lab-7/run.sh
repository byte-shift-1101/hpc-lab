#!/usr/bin/env bash

set -euo pipefail

# Paths (relative to this script directory)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOAL_DIR="${SCRIPT_DIR}/LogGOP-goal"
BIN_DIR="${SCRIPT_DIR}/LogGOP-bin"
RESULTS_CSV="${SCRIPT_DIR}/results.csv"

# Executables (override via env vars if needed)
LOGGOPSIM_BIN="${LOGGOPSIM_BIN:-${SCRIPT_DIR}/LogGOPSim}"
GOAL_TO_BIN_CMD="${GOAL_TO_BIN_CMD:-${SCRIPT_DIR}/txt2bin}"

# LogGOPS parameters
LOGGOPS_L="${LOGGOPS_L:-81999}"
LOGGOPS_o="${LOGGOPS_o:-6678}"
LOGGOPS_g="${LOGGOPS_g:-147220}"
LOGGOPS_G="${LOGGOPS_G:-8}"
LOGGOPS_O="${LOGGOPS_O:-2}"

if [[ ! -d "${GOAL_DIR}" ]]; then
	echo "ERROR: Missing input folder: ${GOAL_DIR}" >&2
	exit 1
fi

if [[ ! -x "${LOGGOPSIM_BIN}" ]]; then
	echo "ERROR: LogGOPSim executable not found or not executable: ${LOGGOPSIM_BIN}" >&2
	exit 1
fi

if [[ ! -x "${GOAL_TO_BIN_CMD}" ]]; then
	echo "ERROR: goal->bin converter not found or not executable: ${GOAL_TO_BIN_CMD}" >&2
	echo "Set GOAL_TO_BIN_CMD to your converter path, for example:" >&2
	echo "  GOAL_TO_BIN_CMD=/path/to/txt2bin ./run.sh" >&2
	exit 1
fi

mkdir -p "${BIN_DIR}"

echo "file,max_host_time" > "${RESULTS_CSV}"

shopt -s nullglob
goal_files=("${GOAL_DIR}"/*.goal)
shopt -u nullglob

if (( ${#goal_files[@]} == 0 )); then
	echo "No .goal files found in ${GOAL_DIR}" >&2
	exit 1
fi

for goal_file in "${goal_files[@]}"; do
	base_name="$(basename "${goal_file}" .goal)"
	bin_file="${BIN_DIR}/${base_name}.bin"

	# 1) Convert .goal -> .bin
	"${GOAL_TO_BIN_CMD}" -i "${goal_file}" -o "${bin_file}"

	# 2) Run LogGOPSim and keep full output in a variable
	output="$("${LOGGOPSIM_BIN}" -f "${bin_file}" \
		--LogGOPS_L "${LOGGOPS_L}" \
		--LogGOPS_o "${LOGGOPS_o}" \
		--LogGOPS_g "${LOGGOPS_g}" \
		--LogGOPS_G "${LOGGOPS_G}" \
		--LogGOPS_O "${LOGGOPS_O}")"

	# 3) Extract result using either format:
	# - Maximum finishing time at host 11: 55305896 (0.0553059 s)
	# - Host 0: 63650324 ... Host N: ... (take max)
	max_host="$(awk '
		/Maximum finishing time at host/ {
			if (match($0, /: [0-9]+/)) {
				print substr($0, RSTART + 2, RLENGTH - 2)
				found = 1
				exit
			}
		}
		$1 == "Host" && $2 ~ /^[0-9]+:$/ && $3 ~ /^[0-9]+$/ {
			if (!host_found || $3 > host_max) {
				host_max = $3
			}
			host_found = 1
		}
		END {
			if (!found && host_found) {
				print host_max
				exit 0
			}
			if (!found) {
				exit 1
			}
		}
	' <<< "${output}")"

	# 4) Write CSV: filename_without_ext,max_value
	echo "${base_name},${max_host}" >> "${RESULTS_CSV}"
done

echo "Done. Results written to: ${RESULTS_CSV}"
