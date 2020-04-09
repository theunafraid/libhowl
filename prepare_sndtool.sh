#!/bin/sh

CWD_=`pwd`

echo $CWD_

KFR_DIR=./sndfile-tools/kfr

KFR_BUILD_DIR=$KFR_DIR/build

mkdir -p $KFR_BUILD_DIR/kfrlib

cd $KFR_DIR/build

if [ ! -f "`pwd`/kfr_config.h" ]; then

cmake -G"Unix Makefiles" -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=kfrlib ..

make -j2

make install

fi

cd $CWD_

SNDTOOL_DIR=./sndfile-tools

cd $SNDTOOL_DIR

CONFIG_FILE="`pwd`/src/config.h"

echo "Check configuration file... $CONFIG_FILE"

if [ ! -f "$CONFIG_FILE" ]; then

./autogen.sh

./configure

fi

cd $CWD_