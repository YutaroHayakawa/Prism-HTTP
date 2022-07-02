VALE_NAME?=vale0

vale-bpf-native-$(VALE_NAME)-objs := ./vale_bpf_native.o
obj-m:=vale-bpf-native-$(VALE_NAME).o

LINUX_SRC:=/lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS:= \
	-std=gnu11 \
	-Ofast \
	-DVALE_NAME=\"$(VALE_NAME)\" \
	-DCONFIG_NETMAP \
	-DCONFIG_NETMAP_VALE \
	-DWITH_MONITOR \
	-I$(LINUX_SRC)/include \
	-I$(NSRC) \
	-I$(NSRC)/LINUX \
	-I$(NSRC)/sys \
	-I$(PWD)/../include

EXTRA_SYMBOLS:= \
	$(NSRC)/Module.symvers

vale_bpf_native:
ifeq ($(VALE_NAME),)
	@echo "Please specify VALE_NAME variable like VALE_NAME=vale0"
else
	make -C $(LINUX_SRC) M=$(CURDIR) CONFIG_NETMAP=m \
		EXTRA_CFLAGS='$(EXTRA_CFLAGS)' KBUILD_EXTRA_SYMBOLS='$(EXTRA_SYMBOLS)' modules
endif

install:
	make -C $(LINUX_SRC) M=$(CURDIR) modules_install

clean:
	make -C $(LINUX_SRC) M=$(PWD) clean
	rm -rf ./modules.order ./Module.symvers \
		./*.o ./*.ko ./.*.o.cmd ./.*.ko.cmd
