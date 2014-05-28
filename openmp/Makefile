CXX :=g++
CXXFLAGS :=-Wall -D__STDC_CONSTANT_MACROS
LDLIBS :=-lavutil \
		 -lavformat \
		 -lavcodec \
		 -lswscale \
		 -lz \
		 -lavutil \
		 -lm \

SOURCE :=main.cpp rf-time.cc
EXECUTE :=main

LDLIBS += `sdl-config --cflags --libs` -fopenmp
all:
	$(CXX) $(CXXFLAGS) -o $(EXECUTE) $(SOURCE) $(LDLIBS)

clean:
	rm -r $(EXECUTE)
