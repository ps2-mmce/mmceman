IOP_SRC_DIR ?= src/
IOP_INC_DIR ?= include/

### LOG_MC

ifeq ($(MMCEDRV), 1)
  $(info -- MMCEDRV)
  #Build minimal MMCE driver for use in-game
  IOP_BIN = irx/mmcedrv.irx
  IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o exports.o imports.o mmcedrv.o
  IOP_CFLAGS += -DMMCEDRV
else
  $(info -- MMCEMAN)
  #Build full MMCEMAN for use with loader / homebrew
  IOP_BIN = irx/mmceman.irx
  IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o mmce_cmds.o mmce_fs.o imports.o mmceman.o
  #Needed for iomanX
  IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0
endif

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
