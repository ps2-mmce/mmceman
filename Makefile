IOP_BIN = irx/mmceman.irx

IOP_SRC_DIR ?= src/
IOP_INC_DIR ?= include/

IOP_OBJS = mmce_sio2.o mmce_cmds.o mmce_fs.o ioplib.o imports.o  main.o

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
