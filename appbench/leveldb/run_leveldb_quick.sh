#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram
BENCHMARK=fillrandom,readrandom
WORKLOADDESCR="fillrandom-readrandom"

# Create output directories
if [ ! -d "$RESULTDIR" ]; then
	mkdir $RESULTDIR
fi

sudo bash -c "ulimit -u 10000000"
ulimit -n 1000000
sudo sysctl -w fs.file-max=10000000

CLEAN() {
	rm -rf /mnt/ram/*
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
	export PARAFSENV=parafs
	export DEVCORECNT=4
    # RR: 0, CFS: 3
	export SCHEDPOLICY=3
    export HOST_CACHE_LIMIT_ENV=$((32*1024*1024*1024))

	#LD_PRELOAD=$DEVFSCLIENT/libshim/shim_common.so ./db_bench_hostcache --db=$DBPATH --benchmarks=$BENCHMARK --use_existing_db=0 --num=500000 --value_size=$2 --threads=$1  --open_files=256 --write_buffer_size=67108864
	LD_PRELOAD=$DEVFSCLIENT/libshim/shim_common.so ./db_bench_hostcache --db=$DBPATH --benchmarks=$BENCHMARK --use_existing_db=0 --num=200000 --value_size=$2 --threads=$1  --open_files=256
	unset PARAFSENV
    unset CACHE_LIMIT_ENV
	sleep 2
}

#declare -a sizearr=("100" "512" "1024" "4096")
declare -a sizearr=("4096")
#declare -a threadarr=("1" "4")
declare -a threadarr=("4")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
