
INCLUDES		:= -I/usr/local/include/cairo -I/usr/local/include

LIB_DIR			:= /usr/local/lib

TARGET_NAME		:= libhowl.a

RINGSPANLINE_DIR:= ./ring-span-line

INCLUDE_RINGSPAN:= $(RINGSPANLINE_DIR)/include

SNDTOOL_DIR		:= ./sndfile-tools

INCLUDE_SNDTOOL := $(SNDTOOL_DIR)/include
SRC_SNDTOOL		:= $(SNDTOOL_DIR)/src

KFR_DIR			:= $(SNDTOOL_DIR)/kfr/build/kfrlib

ZNCC_DIR		:= ./zncc

SOUNDIO_DIR		:= ./libsoundio

CC				:= cc
CFLAGS			:= -Wall -I$(INCLUDE_SNDTOOL) -I$(KFR_DIR)/include $(INCLUDES) -I$(SRC_SNDTOOL) -std=c11 -DSPECTROGRAM_LIB
CXXFLAGS		:= -Wall -I$(INCLUDE_SNDTOOL) -I$(KFR_DIR)/include -I$(ZNCC_DIR) $(INCLUDES) -I. -I$(INCLUDE_RINGSPAN) -O3 -g -std=gnu++11 -pthread -DGPU_SUPPORT -DSPECTROGRAM_LIB

OS				:=$(shell uname)

ifeq ($(OS), Darwin)
LDFLAGS			:=-framework OpenCL -lcairo -lfftw3 -lsndfile -L$(LIB_DIR) -L$(KFR_DIR)/lib -lc++ -framework CoreAudio -framework Foundation -framework AudioToolbox
else
LDFLAGS			:=-lOpenCL
endif

SNDTOOL_SRCFILES:= $(SRC_SNDTOOL)/spectrogram.c $(SRC_SNDTOOL)/window.c $(SRC_SNDTOOL)/spectrum.c $(SRC_SNDTOOL)/common.c
SNDTOOL_OBJFILES:= $(SNDTOOL_SRCFILES:.c=.o)

ZNCC_SRCFILES	:= $(filter-out $(ZNCC_DIR)/main.cpp, $(wildcard $(ZNCC_DIR)/*.cpp))
ZNCC_OBJFILES	:= $(ZNCC_SRCFILES:.cpp=.o)

HOWL_SRCFILES	:= $(wildcard ./lib/*.cpp)
HOWL_OBJFILES	:= $(HOWL_SRCFILES:.cpp=.o)

zncc.o: $(ZNCC_OBJFILES)

configure_sndtool:
	./prepare_sndtool.sh

sndtool.o: $(SNDTOOL_OBJFILES)

howl.o: $(HOWL_OBJFILES)

lib: configure_sndtool sndtool.o zncc.o howl.o
	ar rcs libhowl.a $(SNDTOOL_OBJFILES) $(HOWL_OBJFILES) $(ZNCC_OBJFILES)

rebuild:
	rm -rf $(SNDTOOL_DIR)/src/*.o
	rm -rf lib/*.o
	rm -rf ./*.a
	make lib

soundio:
ifeq (,$(wildcard $(SOUNDIO_DIR)/build/config.h))
	mkdir -p $(SOUNDIO_DIR)/build
	cmake -B$(SOUNDIO_DIR)/build -S$(SOUNDIO_DIR)
	make -C $(SOUNDIO_DIR)/build
endif

test: soundio
	$(shell cp $(ZNCC_DIR)/zncc.cl ./test/zncc.cl)
	g++ -I./lib -I$(SOUNDIO_DIR) -std=c++11 -I$(INCLUDE_RINGSPAN) -I/usr/local/include test/main.cpp libhowl.a $(SOUNDIO_DIR)/build/libsoundio.a $(LDFLAGS) -o test/howl

clean:
	rm -rf $(SOUNDIO_DIR)/build
	rm -rf $(ZNCC_DIR)/*.o
	rm -rf $(SNDTOOL_DIR)/src/*.o
	rm -rf lib/*.o
	rm -rf ./*.a