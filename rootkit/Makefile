obj-m += rootkit.o

ifdef CENTOS
EXTRA_CFLAGS += "-DCENTOS"
endif

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
