
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

FILENUM=8
FILESIZE=(32*1024) #MB
HOSTCACHESIZE=0
DEVCACHESIZE=10
#RESULTDIR=$RESULTS/microbench/posix_io_cache_lamda

# 1. Random read; 2. Random write
declare -a workloadarr=("randread" "randwrite" "seqread" "seqwrite")
declare -a workloadidarr=("2" "3")
declare -a threadarr=("1" "4" "16" "32")

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
        export DEV_CACHE_LIMIT_ENV=$((($DEVCACHESIZE)*1024*1024*1024))

        $MICROBENCH/build/test_smart_cache_posix_hybrid_seq $2 1024 $1 $3 $1 | tee $output/result.txt
        #cat $output/result.txt
        unset HOST_CACHE_LIMIT_ENV
        unset DEV_CACHE_LIMIT_ENV

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
