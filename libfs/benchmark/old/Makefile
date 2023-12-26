all: test_smart_cache_host test_smart_cache_kernel test_smart_cache_hybrid test_no_cache #test_selective_offload_cache_multi_files #test_checksum test_checksum_offload #test_checksum_posix_compound test_checksum_posix test_openwriteclose_offload test_openwriteclose_posix test_crash test_crash_multi test_recovery_ext4 test_encryption test_encryption_posix 

CODE=.
CXX=g++
CC=gcc
LIB_S=-lpthread -lrt -L../ -lcrfslib #-L./libfs/interval_tree -lintervaltreelib

test_checksum: test_checksum.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DWRITE_SEPARATE test_checksum.c ../crc32/crc32.c -o test_checksum $(LIB_S)

test_smart_cache_host: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND test_smart_cache.c ../crc32/crc32.c -o test_smart_cache_host $(LIB_S)

test_smart_cache_kernel: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DKERNEL_CACHE test_smart_cache.c ../crc32/crc32.c -o test_smart_cache_kernel $(LIB_S)

test_smart_cache_hybrid: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DHYBRID_CACHE test_smart_cache.c ../crc32/crc32.c -o test_smart_cache_hybrid $(LIB_S)

test_no_cache: test_smart_cache.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -DNO_CACHE test_smart_cache.c ../crc32/crc32.c -o test_no_cache $(LIB_S)

#test_selective_offload_cache_multi_files: test_selective_offload_cache_multi_files.c
#	        $(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND test_selective_offload_cache_multi_files.c ../crc32/crc32.c -o test_selective_offload_cache_multi_files $(LIB_S)

test_checksum_offload: test_checksum.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND test_checksum.c ../crc32/crc32.c -o test_checksum_offload $(LIB_S)

test_checksum_posix_compound: test_checksum.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE -DCOMPOUND -D_POSIX test_checksum.c ../crc32/crc32.c -o test_checksum_posix_compound $(LIB_S)

test_checksum_posix: test_checksum.c
	$(CC) $(INCLUDE) $(FLAGS) -DVALUE_SIZE  -DWRITE_SEPARATE -D_POSIX test_checksum.c ../crc32/crc32.c -o test_checksum_posix $(LIB_S)

test_openwriteclose_offload: test_openwriteclose.c
	$(CC) $(INCLUDE) $(FLAGS) -DCOMPOUND test_openwriteclose.c ../crc32/crc32.c -o test_openwriteclose_offload $(LIB_S)

test_openwriteclose_posix: test_openwriteclose.c
	$(CC) $(INCLUDE) $(FLAGS) -D_POSIX test_openwriteclose.c ../crc32/crc32.c -o test_openwriteclose_posix $(LIB_S)

test_encryption: test_encryption.c
	$(CC) $(INCLUDE) -g test_encryption.c -o test_encryption $(LIB_S) -lkcapilib

test_encryption_posix: test_encryption.c
	$(CC) $(INCLUDE) -g -D_POSIX test_encryption.c -o test_encryption_posix $(LIB_S) -lkcapilib

test_crash: test_crash.c
	$(CC) $(INCLUDE) $(FLAGS) test_crash.c ../crc32/crc32.c -o test_crash $(LIB_S)

test_crash_multi: test_crash_multi.c
	$(CC) $(INCLUDE) $(FLAGS) test_crash_multi.c ../crc32/crc32.c -o test_crash_multi $(LIB_S)

test_recovery_ext4: test_recovery_ext4.c
	$(CC) $(INCLUDE) $(FLAGS) test_recovery_ext4.c ../crc32/crc32.o -o test_recovery_ext4 -lpthread -lrt

test_cache: test_cache.c
	$(CC) $(INCLUDE) $(FLAGS) test_cache.c ../crc32/crc32.c -o test_cache $(LIB_S)

clean:
	rm -rf *.o
	rm -rf *.so
	rm -rf *.so
	rm -rf test_smart_cache_host test_smart_cache_kernel test_smart_cache_hybrid test_no_cache test_checksum test_cache test_encryption test_encryption_posix test_checksum_offload test_checksum_posix_compound test_checksum_posix test_openwriteclose_offload test_openwriteclose_posix test_crash test_crash_multi test_recovery_ext4

