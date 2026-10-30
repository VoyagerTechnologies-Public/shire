#!/bin/bash -xe

export DISPLAY=:1

cd ./42

xterm -e bash -c 'set -o pipefail; ./42 2>&1 | tee /tmp/fortytwo-run.log' &
xterm &

wait
