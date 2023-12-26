
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=4
FILESIZE=1024 #MB

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
    $MICROBENCH/build/test_direct_posix_io $2 $1 $FILENUM $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    sleep 2
}

# 1. Random read; 2. Random write
declare -a typearr=("0")
declare -a threadarr=("1024" "2048" "3072" "4096")
for size in "${typearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
