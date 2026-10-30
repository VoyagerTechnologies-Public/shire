#!/bin/sh
set -eu
run_id=$1
export SHIRE_BARRIER_PROFILE=$2
. /etc/shire-board-env
export SIMULITH_PUB_ADDR="tcp://127.0.0.1:$SHIRE_BOARD_SYNC_PUB"
export SIMULITH_REP_ADDR="tcp://127.0.0.1:$SHIRE_BOARD_SYNC_REP"
export SIMULITH_SYNC_TRANSPORT=zmq
export SIMULITH_DEVICE_TRANSPORT=tcp
export SIMULITH_DEVICE_HOST=127.0.0.1
export SIMULITH_LOG_MODE=none
export SHIRE_SYNCHRONIZE_GROUND_OUTPUT=1
export LD_LIBRARY_PATH=/opt/shire/app
mkdir -p "/opt/shire/runs/$run_id"
cp -a /opt/shire/seed "/opt/shire/runs/$run_id/app"
cd "/opt/shire/runs/$run_id/app"
export LD_LIBRARY_PATH="$PWD"
/usr/bin/shire-board-udp "$SHIRE_BOARD_TUNNEL" "$SHIRE_BOARD_COMMAND" "$SHIRE_BOARD_TELEMETRY" &
bridge=$!
cleanup() {
    kill "$bridge" "$core" 2>/dev/null || true
    wait "$bridge" "$core" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 143' TERM HUP INT
echo "SHIRE_BOARD_START $SHIRE_BOARD_NAME $SHIRE_BOARD_SYSTEM host-simulated-devices"
"./$SHIRE_BOARD_EXECUTABLE" &
core=$!
wait "$core"
