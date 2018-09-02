ifneq ($(KERNELRELEASE),)
obj-m:=scull0.o
else
#generate the path
CURRENT_PATH:=$(shell pwd)
#the absolute path
LINUX_KERNEL_PATH:=/home/jinfa/gec/linux-2.6.35.7-gec-v3.0-gt110
#complie object
default:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules
clean:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean
endif