CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -g -Wno-missing-field-initializers
BTSTACK_ROOT ?= ../../../../external/bluepad32/external/btstack
STUBS := ../../../wiimote_anything/tests/host
INCLUDES := -I. -I../../include -I../../../yucca_core/test/host -I$(STUBS) -I$(BTSTACK_ROOT)/src -I$(BTSTACK_ROOT)/src/classic \
	-I$(BTSTACK_ROOT)/3rd-party/bluedroid/encoder/include -I$(BTSTACK_ROOT)/3rd-party/bluedroid/decoder/include \
	-I$(BTSTACK_ROOT)/3rd-party/yxml

.PHONY: test clean
test: test_bt_hid_device
	./test_bt_hid_device

btstack_util.o: $(BTSTACK_ROOT)/src/btstack_util.c
	$(CC) -std=gnu11 -g $(INCLUDES) -c $< -o $@

sdp_util.o: $(BTSTACK_ROOT)/src/classic/sdp_util.c
	$(CC) -std=gnu11 -g $(INCLUDES) -c $< -o $@

test_bt_hid_device: test_bt_hid_device.cpp ../../src/bt_hid_device.cpp ../../include/yucca/bt_hid_device.h btstack_util.o sdp_util.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_bt_hid_device.cpp btstack_util.o sdp_util.o

clean:
	rm -f test_bt_hid_device *.o
