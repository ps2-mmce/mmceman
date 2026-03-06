IOP_SRC_DIR ?= ../mmceman/src/
IOP_INC_DIR ?= ../mmceman/include/

$(info -- MMCEFHI)
#Build minimal MMCE driver for use in-game in neutrino
IOP_BIN = irx/mmcefhi.irx
IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o exports.o imports.o mmcedrv.o mmce_fhi.o
IOP_CFLAGS += -DMMCEDRV -DFHI -I../../../apps/neutrino/iop/common/

all: $(IOP_BIN)

clean:
	rm -f -r $(IOP_OBJS) $(IOP_BIN)

ifeq ($(DEBUG), 1)
  $(info -- MMCEMAN debug enabled)
  IOP_CFLAGS += -DDEBUG
  IOP_CFLAGS += -DDEBUG=1
endif

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
