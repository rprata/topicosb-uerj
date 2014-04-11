CXX :=g++
CXXFLAGS :=-Wall -D__STDC_CONSTANT_MACROS
LDLIBS := -lavutil \
		  -lavformat \
		  -lavcodec \
		  -lswscale \
		  -lz \
		  -lavutil \
		  -lm
SOURCE :=main.cpp
EXECUTE :=main

all:
	$(CXX) -o $(EXECUTE) $(SOURCE) $(CXXFLAGS) $(LDLIBS)

clean:
	rm -r $(EXECUTE)
