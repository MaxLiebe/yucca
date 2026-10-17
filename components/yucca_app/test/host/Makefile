CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
CORE := ../../../yucca_core
# esp_log.h stand-in shared with WiimoteAnything's host tests.
HOST := ../../../wiimote_anything/tests/host
SRCS := ../../src/app.cpp $(CORE)/src/settings.cpp $(CORE)/src/settings_schema.cpp $(CORE)/src/gamepad.cpp

.PHONY: test clean
test: test_app
	./test_app

test_app: test_app.cpp $(SRCS) ../../include/yucca/app.h
	$(CXX) $(CXXFLAGS) -I../../include -I$(CORE)/include -I$(HOST) -o $@ test_app.cpp $(SRCS)

clean:
	rm -f test_app
