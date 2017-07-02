obj-m += sun4i-dai.o

KVER ?= $(shell uname -r)

all:
	make -C /lib/modules/$(KVER)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KVER)/build M=$(PWD) clean

install:
	cp sun4i-dai.ko /lib/modules/$(KVER)/kernel/sound/soc/sunxi/
	depmod
