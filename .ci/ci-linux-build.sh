#!/bin/sh

#COPTS="--with-tests=regular --disable-cxx-bindings"
COPTS="--with-tests=regular"
PARALLEL_JOBS=10

# Normal build test of all targets
./autogen.sh $COPTS $@
make -j $PARALLEL_JOBS
make -j $PARALLEL_JOBS examples
make -j $PARALLEL_JOBS benchmark

#make -j 10 dist && mkdir tmp && cp efl*.gz tmp/ && cd tmp/ && tar xf efl* && cd efl*
#./configure $COPTS && make -j 10 && make -j 10 examples && make -j 10 benchmark && make -j 10 check && cd ../..

# Prepare test setup
#export DISPLAY=:99.0
#sh -e /etc/init.d/xvfb start
#sleep 3
#eval $(dbus-launch --sh-syntax --exit-with-session)
#make -j 10 check
#cat src/tests/ecore/*log

#make -j 10 distcheck
