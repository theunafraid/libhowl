
INCLUDES		:= -I/usr/local/include/cairo -I/usr/local/include

LIB_DIR			:= /usr/local/lib

TARGET_NAME		:= libhowl.a

RINGSPANLINE_DIR:= ./ring-span-line

INCLUDE_RINGSPAN:= $(RINGSPANLINE_DIR)/include

SNDTOOL_DIR		:= ./sndfile-tools

INCLUDE_SNDTOOL := $(SNDTOOL_DIR)/include
SRC_SNDTOOL		:= $(SNDTOOL_DIR)/src

ZNCC_DIR		:= ./zncc

CC				:= cc
CFLAGS			:= -Wall -I$(INCLUDE_SNDTOOL) $(INCLUDES) -I$(SRC_SNDTOOL) -DSPECTROGRAM_LIB
CXXFLAGS		:= -Wall -I$(INCLUDE_SNDTOOL) -I$(ZNCC_DIR) $(INCLUDES) -I. -I$(INCLUDE_RINGSPAN) -O3 -g -std=gnu++11 -pthread -DGPU_SUPPORT -DSPECTROGRAM_LIB

OS				:=$(shell uname)

ifeq ($(OS), Darwin)
LDFLAGS			:=-framework OpenCL -lcairo -lfftw3 -lsndfile -L$(LIB_DIR) -lc++
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

test1:
	g++ -I./lib -std=c++11 -I$(INCLUDE_RINGSPAN) -I/usr/local/include test/main.cpp libhowl.a $(LDFLAGS) -lsdl2 -o test/howl

clean:
	rm -f $(ZNCC_DIR)/*.o
	rm -f $(SNDTOOL_DIR)/src/*.o
	rm -f lib/*.o
	rm -f ./*.a