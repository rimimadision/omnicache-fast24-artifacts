
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram


declare -a workloadarr=("knn")

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

    echo $output

    if [ ! -d "$output"   ]; then
        mkdir -p $output
    fi

    $MICROBENCH/knn/build/knn_direct $1 | tee $output/result.txt 
    sleep 1
}

# 1. Random read; 2. Random write
declare -a typearr=("0")
declare -a threadarr=("16" "32")
for size in "${typearr[@]}"
do
    for thrd in "${threadarr[@]}"
    do
        CLEAN
        FlushDisk
        RUN $thrd $size
    done
done
