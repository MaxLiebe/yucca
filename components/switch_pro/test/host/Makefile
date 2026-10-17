CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
SRCS := ../../src/switch_pro.cpp

.PHONY: test clean
test: test_switch_pro
	./test_switch_pro

test_switch_pro: test_switch_pro.cpp $(SRCS) ../../include/yucca/switch_pro.h
	$(CXX) $(CXXFLAGS) -I../../include -I../../../yucca_core/test/host -o $@ test_switch_pro.cpp $(SRCS)

clean:
	rm -f test_switch_pro
