
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=16
FILESIZE=1024 #MB
HOSTCACHESIZE=4
DEVCACHESIZE=4

# Create output directories
if [ ! -d "$RESULTDIR" ]; then
	mkdir -p $RESULTDIR
fi

CLEAN() {
	rm -rf $DBPATH/*
	sudo killall "db_bench"
	sudo killall "db_bench"
	echo "KILLING Rocksdb db_bench"
}

FlushDisk() {
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
	sudo sh -c "sync"
	sudo sh -c "sync"
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

RUN() {
    export HOST_CACHE_LIMIT_ENV=$((($HOSTCACHESIZE)*1024*1024*1024))
    export DEV_CACHE_LIMIT_ENV=$((($DEVCACHESIZE)*1024*1024*1024))
 
    #valgrind --tool=memcheck --leak-check=full $MICROBENCH/build/test_smart_cache_posix_hybrid $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_hybrid $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #sudo perf record -F 99 
    #numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_hybrid $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
     numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_hybrid $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    sleep 2

    unset HOST_CACHE_LIMIT_ENV
    unset DEV_CACHE_LIMIT_ENV
}

# 0. Random read; 1. Random write
declare -a typearr=("2")
declare -a threadarr=("16")

for size in "${typearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
