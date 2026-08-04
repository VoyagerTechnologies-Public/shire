#!/bin/bash -xe

export DISPLAY=:1

cd ./42 && make clean && make -j2 

xterm -e ./42 &
xterm &

wait
