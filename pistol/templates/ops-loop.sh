#!/usr/bin/env bash
# ops-loop.sh — Periodic workflow runner for Praktor ops workflows
#
# Usage:
#   ./ops-loop.sh monitor-host.yml         # defaults: 60s interval
#   ./ops-loop.sh monitor-host.yml 30      # 30s interval
#   ./ops-loop.sh monitor-processes.yml 10  # 10s interval
#
# Environment:
#   PRAKTOR_BIN     Path to praktor binary (default: praktor in $PATH)
#   ALERT_WEBHOOK   Webhook URL for alerts
#   OPS_LOG_DIR     Directory for log files (default: ./ops-logs)
#
# Stop: Ctrl+C or kill the process.

set -euo pipefail

WORKFLOW="${1:?Usage: ops-loop.sh <workflow.yml> [interval_seconds]}"
INTERVAL="${2:-60}"
PRAKTOR="${PRAKTOR_BIN:-praktor}"
LOG_DIR="${OPS_LOG_DIR:-./ops-logs}"

mkdir -p "$LOG_DIR"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Praktor Ops Loop                                          ║"
echo "║  Workflow : $WORKFLOW"
echo "║  Interval : ${INTERVAL}s"
echo "║  Logs     : $LOG_DIR"
echo "╚══════════════════════════════════════════════════════════════╝"

ITERATION=0

cleanup() {
  echo ""
  echo "[ops-loop] Shutting down after $ITERATION iterations."
  exit 0
}

trap cleanup SIGINT SIGTERM

while true; do
  ITERATION=$((ITERATION + 1))
  TIMESTAMP=$(date +"%Y-%m-%dT%H:%M:%S")
  LOG_FILE="$LOG_DIR/run_${ITERATION}_$(date +%Y%m%d_%H%M%S).log"

  echo "[$TIMESTAMP] Iteration #$ITERATION — running $WORKFLOW"

  if "$PRAKTOR" -f "$WORKFLOW" --concurrent > "$LOG_FILE" 2>&1; then
    echo "[$TIMESTAMP] ✓ Completed successfully"
  else
    echo "[$TIMESTAMP] ✗ Completed with failures (see $LOG_FILE)"
  fi

  # Rotate: keep last 100 log files
  LOG_COUNT=$(find "$LOG_DIR" -name "run_*.log" | wc -l)
  if [ "$LOG_COUNT" -gt 100 ]; then
    find "$LOG_DIR" -name "run_*.log" | sort | head -n $((LOG_COUNT - 100)) | xargs rm -f
  fi

  sleep "$INTERVAL"
done
