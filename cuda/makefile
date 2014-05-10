EXECUTABLE := main

SOURCES := $(EXECUTABLE).cu rf-time.cc
OBJS    := $(EXECUTABLE).o  rf-time.o

CUDAPATH     := /usr/local/cuda
SDKPATH      := $(CUDAPATH)/SDK

CC := gcc
CXX := g++
NVCC := $(CUDAPATH)/bin/nvcc

# Usar este para arquitetura de 64 bits
ARCHITECTURE := -L/usr/lib/nvidia-current -L$(CUDAPATH)/lib64 
# Usar este para arquitetura de 32 bits
#ARCHITECTURE := -L/usr/lib/nvidia-current -L/usr/local/cuda/lib -L$(CUDAPATH)/lib -lcutil_i386 

LDFLAGS   := -L$(SDKPATH)/C/common/lib/linux -L$(SDKPATH)/shared/lib/linux -L$(SDKPATH)/C/lib $(ARCHITECTURE) \
             -lcuda -lcudart \
             -lavutil \
		 	 -lavformat \
		 	 -lavcodec \
		 	 -lswscale \
		     -lz \
			 -lavutil \
			 -lm \
			`sdl-config --cflags --libs`

CXFLAGS   := -I$(SDKPATH)/C/common/inc -I$(CUDAPATH)/include -I$(SDKPATH)/shared/inc -I$(SDKPATH)/C/common/inc

# 48K shared  16K L1 cache
NVCCFLAGS := -arch sm_11
# 48K shared  16K L1 cache
NVCCFLAGS := -arch sm_11 
# 16K shared  48K L1 cache
#NVCCFLAGS := -arch sm_11 -Xptxas -dlcm=ca
# 64K shared   0K L1 cache
#NVCCFLAGS := -arch sm_11 -Xptxas -dlcm=cg
#NVCCFLAGS := --compiler-bindir /usr/bin/gcc-4.3 -arch sm_11

$(EXECUTABLE): $(EXECUTABLE).cu 
	$(NVCC) $(NVCCFLAGS) $(LDFLAGS) $(CXFLAGS) -c $(SOURCES)
	$(NVCC) $(NVCCFLAGS) $(LDFLAGS) $(CXFLAGS) -o $(EXECUTABLE) $(OBJS)

depend:
	$(CC) $(CXXFLAGS) -M *.cc > .depend

clean:
	rm -f *.o *~ .depend $(EXECUTABLE)

all: clean depend $(EXECUTABLE)

ifeq (.depend,$(wildcard .depend))
include .depend
endif
