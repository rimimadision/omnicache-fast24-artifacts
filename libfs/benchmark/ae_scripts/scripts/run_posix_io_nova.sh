#! /bin/bash
set -x

DBPATH=/mnt/pmemdir

FILENUM=1
FILESIZE=(32*1024) #MB

declare -a workloadarr=("randread" "randwrite" "seqread" "seqwrite")
declare -a workloadidarr=("0" "1")
#declare -a workloadidarr=("0")
declare -a threadarr=("1" "4" "16" "32")

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

        if [ ! -d "$output"  ]; then
                mkdir -p $output
        fi


    $MICROBENCH/build/test_direct_posix_fs $2 4096 $1 $FILESIZE $FILENUM &> $output/result.txt  #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
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
