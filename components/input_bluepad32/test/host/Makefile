CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
CORE := ../../../yucca_core
SRCS := ../../src/convert.cpp $(CORE)/src/gamepad.cpp

.PHONY: test clean
test: test_convert
	./test_convert

test_convert: test_convert.cpp $(SRCS) ../../src/convert.h
	$(CXX) $(CXXFLAGS) -I../../src -I$(CORE)/include -o $@ test_convert.cpp $(SRCS)

clean:
	rm -f test_convert
