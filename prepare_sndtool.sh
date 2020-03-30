#!/bin/sh

CWD_=`pwd`

echo $CWD_

SNDTOOL_DIR=./sndfile-tools

cd $SNDTOOL_DIR

./autogen.sh

./configure

cd $CWD_