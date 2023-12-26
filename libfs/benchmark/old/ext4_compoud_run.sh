
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/pmemdir

FSTYPE="Ext4"
APP="microbench"
RESULTDIR=$RESULTS/$FSTYPE/$APP/"result-ext4"

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
    ./test_checksum_posix_compound $2 4096 $1 #&> $RESULTDIR/"crossfs_"$2"_"$1"_4096.txt"
	sleep 2
}

#declare -a sizearr=("100" "512" "1024" "4096")
declare -a sizearr=("2" "4")
#declare -a threadarr=("1" "4")
declare -a threadarr=("1" "4" "8")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
