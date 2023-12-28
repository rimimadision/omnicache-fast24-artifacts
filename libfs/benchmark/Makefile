all: test_smart_cache_cisc_host test_smart_cache_cisc_hybrid test_direct_cisc_io test_smart_cache_posix_host test_smart_cache_posix_hybrid test_direct_posix_io test_smart_cache_posix_hybrid_cxl test_direct_posix_fs test_smart_cache_posix_hybrid_seq test_smart_cache_posix_host_seq

CODE=.
CXX=g++
CC=gcc
LIB_S=-lpthread -lrt -L../ #-lcrfslib #-L./libfs/interval_tree -lintervaltreelib
BUILD=./build

test_smart_cache_cisc_host: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_cisc_host $(LIB_S) -lcrfslib_wb_host

#test_smart_cache_cisc_kernel: test_smart_cache.c
#	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DKERNEL_CACHE test_smart_cache.c ../crc32/crc32.c -o test_smart_cache_kernel $(LIB_S) -lcrfslib_wb_host

test_smart_cache_cisc_hybrid: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DHYBRID_CACHE test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_cisc_hybrid $(LIB_S) -lcrfslib_wb_hybrid

test_direct_posix_io: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_direct_posix_io $(LIB_S) -lcrfslib

test_smart_cache_posix_host: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_posix_host $(LIB_S) -lcrfslib_wb_host

test_smart_cache_posix_host_seq: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX -DCRFS_SEQ test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_posix_host_seq $(LIB_S) -lcrfslib_wb_host

test_smart_cache_posix_hybrid: test_smart_cache.c 
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_posix_hybrid $(LIB_S) -lcrfslib_wb_hybrid

test_smart_cache_posix_hybrid_seq: test_smart_cache.c 
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX -DCRFS_SEQ test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_posix_hybrid_seq $(LIB_S) -lcrfslib_wb_hybrid

test_smart_cache_posix_hybrid_cxl: test_smart_cache.c 
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCRFS_POSIX test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_smart_cache_posix_hybrid_cxl $(LIB_S) -lcrfslib_wb_hybrid_cxl

test_direct_cisc_io: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DNO_CACHE test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_direct_cisc_io $(LIB_S) -lcrfslib

test_direct_posix_fs: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -D_POSIX test_smart_cache.c ../crc32/crc32.c -o $(BUILD)/test_direct_posix_fs $(LIB_S) -lcrfslib

#check_cache: check_cache.c
#		$(CC) $(INCLUDE) $(FLAGS) check_cache.c ../crc32/crc32.c -o check_cache $(LIB_S) -lcrfslib_wb_host

clean:
	rm -rf *.o
	rm -rf *.so
	rm -rf *.so
	rm -rf $(BUILD)/test*
	#rm -rf test_smart_cache_cisc_host test_smart_cache_cisc_hybrid test_direct test_smart_cache_posix_host test_smart_cache_posix_hybrid test_direct_posix_io test_direct_cisc_io #check_cache test_cache

