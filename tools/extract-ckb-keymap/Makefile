# Builds the keymap extractor against a ckb-next checkout.
#
#   nix develop .#keymap
#   make CKB=/path/to/ckb-next
#
# CKB must point at a ckb-next source tree; only src/gui/keymap.{h,cpp} are used.

CKB ?= $(error set CKB to a ckb-next checkout, e.g. make CKB=../../../ckb-next)

CXXFLAGS := -std=c++17 -fPIC -O2 -I$(CKB)/src/gui $(shell pkg-config --cflags Qt6Core Qt6Gui)
LDLIBS := $(shell pkg-config --libs Qt6Core Qt6Gui)

extract-ckb-keymap: main.cpp $(CKB)/src/gui/keymap.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: clean
clean:
	rm -f extract-ckb-keymap
