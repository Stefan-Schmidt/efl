#!/bin/sh

COPTS="--with-tests=regular"
PARALLEL_JOBS=10

# Prepare test setup
#export DISPLAY=:99.0
#sh -e /etc/init.d/xvfb start
#sleep 3
#eval $(dbus-launch --sh-syntax --exit-with-session)
#./autogen.sh $COPTS $@
make -j $PARALLEL_JOBS check
#cat src/tests/ecore/*log
