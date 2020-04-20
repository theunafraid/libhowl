#!/bin/sh

CWD_=`pwd`

echo $CWD_

mkdir ./arrayfire

wget -c https://arrayfire.s3.amazonaws.com/3.6.4/ArrayFire-v3.6.4_OSX_x86_64.pkg -O ./arrayfire/ArrayFire-OSX.pkg

sudo installer -pkg ./arrayfire/ArrayFire-OSX.pkg -target /

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
