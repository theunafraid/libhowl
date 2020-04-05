#!/bin/sh

CWD_=`pwd`

echo $CWD_

SNDTOOL_DIR=./sndfile-tools

cd $SNDTOOL_DIR

CONFIG_FILE="`pwd`/src/config.h"

echo "Check configuration file... $CONFIG_FILE"

if [ ! -f "$CONFIG_FILE" ]; then

./autogen.sh

./configure

fi

cd $CWD_