CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g
CORE := ../../../yucca_core
# bt_hid_device.h needs BTstack's headers; esp_log.h etc. come from the WiimoteAnything stubs.
BTSTACK_ROOT ?= ../../../../external/bluepad32/external/btstack
INCLUDES := -I. -I../../include -I$(CORE)/include -I$(CORE)/test/host -I../../../bt_hid_device/include \
	-I../../../switch_pro/include -I../../../wiimote_anything/tests/host \
	-I$(BTSTACK_ROOT)/src -I$(BTSTACK_ROOT)/src/classic
SRCS := ../../src/switch_target.cpp ../../../switch_pro/src/switch_pro.cpp \
	$(CORE)/src/mapper.cpp $(CORE)/src/settings.cpp $(CORE)/src/gamepad.cpp

.PHONY: test clean
test: test_switch
	./test_switch

test_switch: test_switch.cpp $(SRCS) ../../include/yucca/switch_target.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_switch.cpp $(SRCS)

clean:
	rm -f test_switch
