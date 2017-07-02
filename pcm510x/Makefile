obj-m += pcm510x.o

KVER ?= $(shell uname -r)

all:
	make -C /lib/modules/$(KVER)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KVER)/build M=$(PWD) clean

install:
	cp pcm510x.ko /lib/modules/$(KVER)/kernel/sound/soc/sunxi/
	depmod
