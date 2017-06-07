#!/bin/sh

COPTS="--with-tests=regular"
PARALLEL_JOBS=10

# Preapre dist tarball and check if all targets build from within this dist
./autogen.sh $COPTS $@
make -j $PARALLEL_JOBS dist
mkdir tmp/
tar xf efl*gz -C tmp/
cd tmp/efl*
./configure $COPTS
make -j $PARALLEL_JOBS
make -j $PARALLEL_JOBS examples
make -j $PARALLEL_JOBS benchmark
make -j $PARALLEL_JOBS check
cd ../..

# Do a full distcheck run at the end
make -j $PARALLEL_JOBS distcheck
