CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
CORE := ../../../yucca_core
WM := ../../../wiimote_anything
# WiimoteAnything's headers need BTstack's (for wiimote_btstack.h).
BTSTACK_ROOT ?= ../../../../external/bluepad32/external/btstack
INCLUDES := -I. -I../../include -I$(CORE)/include -I$(WM)/tests/host -I$(WM)/include \
	-I$(BTSTACK_ROOT)/src -I$(BTSTACK_ROOT)/src/classic
SRCS := ../../src/wii_target.cpp fake_wiimote.cpp $(CORE)/src/mapper.cpp $(CORE)/src/settings.cpp $(CORE)/src/gamepad.cpp

.PHONY: test clean
test: test_wii
	./test_wii

test_wii: test_wii.cpp $(SRCS) fake_wiimote.h ../../include/yucca/wii_target.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_wii.cpp $(SRCS)

clean:
	rm -f test_wii
