
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

FILENUM=1
FILESIZE=(16*1024) #MB

RESULTDIR=$RESULTS/microbench/direct_cisc_io

# 1. Random read; 2. Random write
declare -a workloadarr=("readchksmwrite")
declare -a workloadidarr=("5")
declare -a threadarr=("1" "4" "8" "16")

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
        output="$RESULTDIR/$workload/$1"

        if [ ! -d "$output"  ]; then
                mkdir -p $output
        fi

        $MICROBENCH/build/test_direct_cisc_io $2 4096 $1 $3 $1 &> $output/result.txt 
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
