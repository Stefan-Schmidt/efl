#!/bin/sh

COPTS="--with-tests=regular"
PARALLEL_JOBS=10

# Normal build test of all targets
./autogen.sh $COPTS $@
make -j $PARALLEL_JOBS
make -j $PARALLEL_JOBS examples
make -j $PARALLEL_JOBS benchmark

# Prepare test setup
#export DISPLAY=:99.0
#sh -e /etc/init.d/xvfb start
#sleep 3
#eval $(dbus-launch --sh-syntax --exit-with-session)
#make -j 10 check
#cat src/tests/ecore/*log
