
all:
	$(MAKE) -C mmceman
	$(MAKE) -C mmcedrv
	$(MAKE) -C mmceigr
	$(MAKE) -C mmcefhi

clean:
	$(MAKE) -C mmceman clean
	$(MAKE) -C mmcedrv clean
	$(MAKE) -C mmceigr clean
	$(MAKE) -C mmcefhi clean

install:
	cp mmceman/irx/mmceman.irx $(PS2SDK)/iop/irx/
	cp mmcedrv/irx/mmcedrv.irx $(PS2SDK)/iop/irx/
	cp mmceigr/irx/mmceigr.irx $(PS2SDK)/iop/irx/
	cp mmcefhi/irx/mmcefhi.irx $(PS2SDK)/iop/irx/
