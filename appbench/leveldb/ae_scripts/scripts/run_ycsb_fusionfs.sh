#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram
BENCHMARK=load,ycsba,ycsbb,ycsbc,ycsbf,ycsbd,ycsbe 
KEYS=200000

# Create output directories
if [ ! -d "$RESULTDIR" ]; then
	mkdir $RESULTDIR
fi

sudo bash -c "ulimit -u 10000000"
ulimit -n 1000000
sudo sysctl -w fs.file-max=10000000


#$BASE/appbench/leveldb/ae_scripts/scripts/kill_leveldb_fusionfs.sh &
#echo "BEGIN TO RUN"

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
        output=$AERESULTDIR

        if [ ! -d "$output"   ]; then
                mkdir -p $output
        fi

        export LD_PRELOAD=$DEVFSCLIENT/libshim/shim_common.so
        $LEVELDB/db_bench_fusionfs --db=$DBPATH --benchmarks=$BENCHMARK --use_existing_db=0 --num=$KEYS --ycsb_ops_num=$KEYS --value_size=$2 --threads=$1 --open_files=256 | tee $output/result.txt
        export LD_PRELOAD=""

        unset PARAFSENV
        sleep 2
}

#declare -a sizearr=("100" "512" "1024" "4096")
declare -a sizearr=("1024")
#declare -a threadarr=("1" "4")
declare -a threadarr=("1")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
