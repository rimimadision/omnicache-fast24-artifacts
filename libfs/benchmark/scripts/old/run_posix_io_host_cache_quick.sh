
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=16
FILESIZE=1024 #MB
HOSTCACHESIZE=8

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
    #export HOST_CACHE_LIMIT_ENV=$((($HOSTCACHESIZE)*1024*1024))
    #valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #numactl --physcpubind=0-15,32-47 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    unset HOST_CACHE_LIMIT_ENV
    sleep 2
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
