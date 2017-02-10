KDIR ?= /lib/modules/$(shell uname -r)/build

BUILD_DIR ?= $(PWD)/build

default:
	make -C $(KDIR) M=$(PWD) src=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) src=$(PWD) clean
