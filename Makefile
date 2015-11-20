CROSS_COMPILE = arm-linux-gnueabihf-
AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC 		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E

OBJDUMP		= $(CROSS_COMPILE)objdump
OBJCOPY		= $(CROSS_COMPILE)objcopy

export AS LD CC CPP
export OBJDUMP OBJCOPY

CFLAGS := -Wall -O2 -g
CFLAGS += -I $(shell pwd)/include

LDFLAGS := -lm -lpthread

export CFLAGS LDFLAGS

TOPDIR := $(shell pwd)
export TOPDIR

TARGET := jc

obj-y += main.o
obj-y += can/
obj-y += protocol/

all :
	make -C ./ -f $(TOPDIR)/Makefile.build
	$(CC) $(LDFLAGS) -o $(TARGET) built-in.o
	
clean:
	rm -f $(shell find -name "*.o")
	rm -f $(TARGET)
	
distclean:
	rm -f $(shell find -name "*.o")
	rm -f $(shell find -name "*.d")
	rm -f $(TARGET)

	