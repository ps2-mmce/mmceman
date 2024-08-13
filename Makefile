
all:
	$(MAKE) -C mmceman
	$(MAKE) -C mmcedrv

clean:
	$(MAKE) -C mmceman clean
	$(MAKE) -C mmcedrv clean