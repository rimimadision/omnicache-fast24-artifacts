
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=4
FILESIZE=4096 #MB
HOSTCACHESIZE=8

declare -a workloadarr=("randread" "randwrite" "seqread" "seqwrite")


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
        workload=${workloadarr[($2)]}
        output="$AERESULTDIR/$workload/$1"
        echo $output

        if [ ! -d "$output"  ]; then
                mkdir -p $output
        fi

        export HOST_CACHE_LIMIT_ENV=$((($HOSTCACHESIZE)*1024*1024*1024))
         numactl --physcpubind=0-15,32-47 --membind=0 $MICROBENCH/build/test_smart_cache_posix_host $2 $1 $FILENUM $FILESIZE $FILENUM &> $output/result.txt #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
        unset HOST_CACHE_LIMIT_ENV
        sleep 2
}

# 0. Random read; 1. Random write
declare -a typearr=("0" "1")
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
