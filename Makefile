obj-m += loop.o

loop-objs := loop_main.o utils.o

ccflags-y := -O3

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
