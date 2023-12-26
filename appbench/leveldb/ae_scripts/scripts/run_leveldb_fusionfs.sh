#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram
BENCHMARK=fillrandom,readrandom
WORKLOADDESCR="fillrandom-readrandom"
AERESULTDIR=$PWD
KEYS=300000

$BASE/appbench/leveldb/ae_scripts/scripts/kill_leveldb_fusionfs.sh &
echo "BEGIN TO RUN"


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
        export HOST_CACHE_LIMIT_ENV=$((16*1024*1024*1024))
        export DEV_CACHE_LIMIT_ENV=$((4*1024*1024*1024))

        output=$AERESULTDIR

        if [ ! -d "$output"   ]; then
                mkdir -p $output
        fi

        LD_PRELOAD=$DEVFSCLIENT/libshim/shim_common.so $LEVELDB/db_bench_fusionfs --db=$DBPATH --benchmarks=$BENCHMARK --use_existing_db=0 --num=$KEYS --value_size=$2 --threads=$1 --open_files=256 | tee $output/result.txt
        unset PARAFSENV
        unset HOST_CACHE_LIMIT_ENV
        unset DEV_CACHE_LIMIT_ENV
	echo "Cooling off"
        sleep 60
}

#declare -a sizearr=("100" "512" "1024" "4096")
declare -a sizearr=("1024")
#declare -a threadarr=("1" "4")
declare -a threadarr=("8")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
