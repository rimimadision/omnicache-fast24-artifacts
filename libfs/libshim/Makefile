SYSCALL_INTERCEPT_DIR := $(abspath $(CURDIR)/syscall_intercept/install/)
INCLUDE=-I$(SYSCALL_INTERCEPT_DIR)/include
SHIMLDFLAGS=-L$(abspath $(SYSCALL_INTERCEPT_DIR)/lib) -Wl,-rpath=$(abspath $(SYSCALL_INTERCEPT_DIR)/lib) -lsyscall_intercept


all: shim.so shim_common.so

shim.so:
	gcc $(INCLUDE) shim.c -lcrfslib $(SHIMLDFLAGS) -fpic -shared -o shim.so

shim_common.so:
	gcc shim_common.c -lcrfslib -ldl -fpic -shared -o shim_common.so

clean:
	rm -rf shim.so shim_common.so syscall_intercept
