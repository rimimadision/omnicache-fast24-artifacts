
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

    if [ ! -d "$output"  ]; then
        mkdir -p $output
    fi

    export HOST_CACHE_LIMIT_ENV=$((16*1024*1024*1024))
    $MICROBENCH/knn/build/knn_host_only $1 | tee $output/result.txt
    unset HOST_CACHE_LIMIT_ENV
    sleep 1
}

declare -a typearr=("0")
declare -a threadarr=("8" "16")
#declare -a threadarr=("4")
for size in "${typearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
