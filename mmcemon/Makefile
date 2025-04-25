IOP_SRC_DIR ?= ../mmceman/src/
IOP_INC_DIR ?= ../mmceman/include/

LOG_SPECIFIC_PORT ?= 3

$(info -- MMCEMON)
#Build single shot module to issue switch to bootcard on IGR
IOP_BIN = irx/mmcemon.irx
IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o mmce_cmds.o mmce_fs.o imports.o mmceman.o
IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0
IOP_CFLAGS += -DMMCEMON
all: $(IOP_BIN)

ifdef LOG_SPECIFIC_PORT
IOP_CFLAGS += -DPORT_TARGET=$(LOG_SPECIFIC_PORT)
endif

ifneq (x$(MMCEMON_STRICT_LOGGING),x0)
IOP_CFLAGS += -DMMCEMON_STRICT_LOGGING=$(MMCEMON_STRICT_LOGGING)
endif

clean:
	rm -f -r $(IOP_OBJS) $(IOP_BIN) irx/*notiopmod*.elf

info:
	$(info --- Build Options:)
	$(info ---- LOG_SPECIFIC_PORT: if defined, the transfer data log is limited to the specified SIO2 Port)
	$(info ---- MMCEMON_STRICT_LOGGING: limits the logging to the transfer data controlled by LOG_SPECIFIC_PORT. excludes general info of other transfers)
	$(info ---- MINIMAL_LOGGER: @NOT IMPLEMEBTED YET@ compile only with the logging mechanism. don't declare MMCE devices nor include code for operating them)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal