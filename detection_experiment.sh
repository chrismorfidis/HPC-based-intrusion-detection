#!/bin/bash
# Step 4 Detection Experiment (C system)
# 1) Collect per-app baseline for a chosen app
# 2) Run that app concurrently with a different app
# 3) Monitor the baseline app's PID with HPC-IDS and verify anomalies

set -euo pipefail

BASE_APP=${1:-matmul}
NOISE_APP=${2:-memory_scan}
DURATION=${DURATION:-15}
CFG="config/rigorous_hpc_config.json"
ALERTS="../logs/hpc_ids_alerts.jsonl"
APPS_DIR="../test_apps"

echo "=== Step 4: Detection Experiment (C) ==="
echo "Base app: $BASE_APP | Noise app: $NOISE_APP | Duration: ${DURATION}s"

# Ensure test binaries exist
if [ ! -x "$APPS_DIR/$BASE_APP" ] || [ ! -x "$APPS_DIR/$NOISE_APP" ]; then
  echo "Building test apps..."
  (cd "$APPS_DIR" && make)
fi

echo "Building C system..."
make -s all

echo "Using config: $CFG"
echo "Collecting baseline for $BASE_APP..."
./baseline_collector --app "$BASE_APP" --runs 5 --config "$CFG"

echo "Clearing alerts..."
mkdir -p ../logs
rm -f "$ALERTS"

echo "Starting noise app in background and monitoring $BASE_APP as target..."
(cd "$APPS_DIR" && timeout "$DURATION" ./$NOISE_APP >/dev/null 2>&1) &
noise_driver=$!

set +e
./hpc_ids --monitor --app-name "$BASE_APP" --duration "$DURATION" --config "$CFG"
rc=$?
set -e
wait $noise_driver || true

if [ $rc -ne 0 ]; then
  echo "Warning: IDS monitoring returned non-zero ($rc). Check perf permissions and config."
fi

echo
echo "=== Alerts for $BASE_APP (if any) ==="
if [ -f "$ALERTS" ]; then
  grep -F '"application_name":"'"$BASE_APP" "$ALERTS" || echo "No per-app alerts logged."
else
  echo "No alerts file found: $ALERTS"
fi

echo "Done."
