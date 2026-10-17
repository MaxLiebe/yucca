CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
SRCS := ../../src/gamepad.cpp ../../src/mapper.cpp ../../src/settings.cpp

.PHONY: test clean
test: test_core
	./test_core

test_core: test_core.cpp $(SRCS) $(wildcard ../../include/yucca/*.h) check.h
	$(CXX) $(CXXFLAGS) -I../../include -o $@ test_core.cpp $(SRCS)

clean:
	rm -f test_core
