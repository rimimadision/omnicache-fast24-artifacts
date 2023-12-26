
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
FILENUM=16
FILESIZE=2048 #MB

declare -a workloadarr=("randread" "randwrite" "seqread" "seqwrite")

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

        if [ ! -d "$output"  ]; then
                mkdir -p $output
        fi

        $MICROBENCH/build/test_direct_posix_io $2 4096 $FILENUM $FILESIZE $FILENUM &> $output/result.txt #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 

        sleep 2
}

# 1. Random read; 2. Random write
declare -a typearr=("0" "1")
declare -a threadarr=("4")
for size in "${typearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
