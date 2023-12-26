all: crfslib

CODE=.
CXX=g++
CC=gcc
LIB_S=-lpthread -lrt -L./interval_tree -lintervaltreelib
SOURCE=$(wildcard *.c)

#FLAGS+=-D_KERNEL_TESTING
#FLAGS+=-D_POSIXIO
#FLAGS+=-g
#FLAGS+=-D_DEBUG
FLAGS+=-D_USE_DEVFS_LIB -D_USE_DEVFS_CALLS #-D_USE_THREADING
#FLAGS+=-D_VERIFY_DATA
#FLAGS+=-DTEMP_SCALE
#FLAGS+=-DSHADOW_FD
FLAGS+=-DPARAFS_SCHED_THREAD
FLAGS+=-DPARAFS_BYPASS_KERNEL
#FLAGS+=-D_USE_OPT
#FLAGS+=-DPCIE_DELAY
#FLAGS+=-DFLUSH_CACHE
#FLAGS+=-DPARAFS_FSYNC_ENABLE
#FLAGS+=-DPARAFS_INTERVAL_TREE
#FLAGS+=-DPARAFS_STAT
#FLAGS+=-DREDUCE_CPU_FREQ
#FLAGS+=-D_NVMFDQ
#FLAGS+=-DPARAFS_SHM
#FLAGS+=-DPARAFS_INTERVAL_TREE
#FLAGS+=-D_USE_VECTOR_IO_CMD
FLAGS+=-DCRFS_OPENCLOSE_OPT
#FLAGS+=-DCACHE_PAGE_GRANULARITY

FLAGS+=-DPERFOPT

#FLAGS+=-DCOMPRESS_CACHE

#FLAGS+=-DCRFS_SYNC_WRITE
#FLAGS+=-DCRFS_WT_CACHE
#FLAGS+=-DCRFS_WB_CACHE
#FLAGS+=-DCACHE_HYBRID

#FLAGS+=-DCACHE_EVICT

#FLAGS+=-DCXL_MEM
#FLAGS+=-DCXL_MEM_NAMESPACE
FLAGS+=-DCACHE_MODEL
#FLAGS+=-DNEARCACHE_OPT

obj-m := libcrfslib.o
KDIR := /lib/modules/`uname -r`/build
PWD := `pwd`

crfslib:
	sudo cp uthash/*.h /usr/local/include
	sudo cp uthash/*.h /usr/local/include
	sudo cp uthash/*.h /usr/lib/
	make -C crc32
	make -C interval-tree
	make install -C interval-tree

	#Direct IO
	gcc -fPIC -shared $(FLAGS) -o libcrfslib.so $(SOURCE) crc32/crc32.o $(LIB_S) 

    #Write Back Cache (Host)
	gcc -fPIC -shared $(FLAGS) -o libcrfslib_wb_host.so -DCACHE_PAGE_GRANULARITY -DCRFS_WB_CACHE -DCACHE_EVICT $(SOURCE) crc32/crc32.o $(LIB_S) 

	gcc -fPIC -shared $(FLAGS) -o libcrfslib_leveldb_host.so -DLEVELDB_CACHE -DCRFS_WB_CACHE $(SOURCE) crc32/crc32.o $(LIB_S) 
	#gcc -fPIC -shared $(FLAGS) -o libcrfslib_wb_host.so -DCRFS_WB_CACHE $(SOURCE) crc32/crc32.o $(LIB_S) 

	#Write Back Cache (Hybrid)
	gcc -fPIC -shared -O3 $(FLAGS) -o libcrfslib_wb_hybrid.so -DCRFS_WB_CACHE -DCACHE_HYBRID -DCACHE_EVICT $(SOURCE) crc32/crc32.o $(LIB_S) 

	gcc -fPIC -shared -O3 $(FLAGS) -o libcrfslib_leveldb_hybrid.so -DCRFS_WB_CACHE -DCACHE_HYBRID -DLEVELDB_CACHE  $(SOURCE) crc32/crc32.o $(LIB_S) 

	#Write Back Cache (Hybrid) + CXL
	gcc -fPIC -shared $(FLAGS) -o libcrfslib_wb_hybrid_cxl.so -DCRFS_WB_CACHE -DCACHE_HYBRID -DCACHE_EVICT -DCXL_MEM $(SOURCE) crc32/crc32.o $(LIB_S) 


install:
	sudo cp *.so /usr/local/lib
	sudo cp *.so /usr/lib
	sudo cp *.h /usr/local/include
	sudo cp *.h /usr/include
	sudo cp *.h /usr/lib/

clean:
	rm -rf *.o
	rm -rf *.so
	rm -rf *.so
	rm -rf interval-tree/*.o
	rm -rf interval-tree/*.so
	rm -rf interval-tree/interval_csv interval-tree/interval-tree-test
