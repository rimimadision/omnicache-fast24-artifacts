
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/pmemdir

APP="microbench"
RESULTDIR=$RESULTS/$APP/"result-ext4"

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
        ./test_openwriteclose_posix $2 0.5 $1 &> $RESULTDIR/"ext4_"$2"_"$1"_512KB.txt"
        #./test_checksum_posix $2 4096 $1 &> $RESULTDIR/"ext4_"$2"_"$1"_4096.txt"
        cat $RESULTDIR/"ext4_"$2"_"$1"_512KB.txt"
	sleep 2
}

#declare -a sizearr=("100" "512" "1024" "4096")
declare -a sizearr=("10000" "50000" "100000")
#declare -a sizearr=("3")
#declare -a threadarr=("1" "4")
declare -a threadarr=("1" "4" "8" "16")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
