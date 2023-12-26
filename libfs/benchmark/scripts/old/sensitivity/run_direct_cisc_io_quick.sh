
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=16
FILESIZE=1024 #MB

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
    for i in {0..31}
    $TOOLS/numactl/numactl --cpunodebind=1 $TOOLS/numactl/memhog -r96 1g --membind 1 & 
    done
    $MICROBENCH/build/test_direct_cisc_io $2 4096 $1 $FILESIZE $FILENUM
    sleep 2
}

# 3. Read-CRC-Write
declare -a typearr=("3")
declare -a threadarr=("16")
for size in "${typearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
