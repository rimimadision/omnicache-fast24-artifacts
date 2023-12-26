
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

FILENUM=4
FILESIZE=(32*1024) #MB
HOSTCACHESIZE=16

#RESULTDIR=$RESULTS/microbench/posix_io_cache_host_only

# 1. Random read; 2. Random write
declare -a workloadarr=("randread" "randwrite" "seqread" "seqwrite")
#declare -a workloadidarr=("0")
declare -a workloadidarr=("0" "1")

#declare -a workloadidarr=("1")
#declare -a threadarr=("1" "8" "16" "32")
declare -a threadarr=("1" "4" "16" "32")

#declare -a workloadidarr=("0" "1")
#declare -a threadarr=("1" "8")

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
        workload=${workloadarr[($2)]}
        output="$AERESULTDIR/$workload/$1"

        if [ ! -d "$output"  ]; then
                mkdir -p $output
        fi

        export HOST_CACHE_LIMIT_ENV=$((($HOSTCACHESIZE)*1024*1024*1024))
        numactl --physcpubind=0-15,32-47 --membind=0 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $3 $1 | tee $output/result.txt 
        #valgrind --tool=memcheck --leak-check=full $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $3 $1 #| tee $output/result.txt 
        #sudo perf record -F 99 $MICROBENCH/build/test_smart_cache_posix_host $2 4096 $1 $3 $1 #| tee $output/result.txt 
        #cat $output/result.txt
        unset HOST_CACHE_LIMIT_ENV
        sleep 2
}

for workloadid in "${workloadidarr[@]}"
do
	for thrd in "${threadarr[@]}"
        do
                per_thd_size=$(($FILESIZE/$thrd))
	        CLEAN
		FlushDisk
		RUN $thrd $workloadid $per_thd_size
	done
done
