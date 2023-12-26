
#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram

APP="microbench"
RESULTDIR=$RESULTS/$APP/"result-cache-wt-noevict"
FILENUM=1
FILESIZE=4096 #MB

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
    #export CACHE_LIMIT_ENV=$((($2)*1024*1024*1024))
    export CACHE_LIMIT_ENV=$((($2)*1024*1024))
    ./test_smart_cache_hybrid 1 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 

    #./test_checksum_offload $52 4096 $1 &> $RESULTDIR/"crossfs_"$2"_"$1"_2M.txt"
    #./test_openwriteclose_offload $2 1 $1 &> $RESULTDIR/"fusionfs_"$2"_"$1"_1M.txt"
    #./test_selective_offload_cache 1 4096 $1 $FILESIZE $FILENUM #&> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt" 
    #./test_no_cache_offload_cache 1 4096 $1 $FILESIZE $FILENUM &> $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt"  
    unset CACHE_LIMIT_ENV
    #cat $RESULTDIR/"fusionfs_cachesize_"$2"_threads_"$1"_filenum_"$FILENUM"_filesize_"$FILESIZE".txt"
    #cat $RESULTDIR/"fusionfs_"$2"_"$1".txt"
    sleep 2
}

#declare -a sizearr=("100" "512" "1024" "4096")
#declare -a sizearr=("10000")
#declare -a cachesizearr=("30" "20" "10" "5")
#declare -a cachesizearr=("32" "16" "8" "4" "2")
#declare -a cachesizearr=("48" "24" "12" "4" "2" "1")
declare -a cachesizearr=("1024")
#declare -a sizearr=("50000")
declare -a threadarr=("1")
#declare -a threadarr=("1" "4" "8" "16" "32" "64")
#declare -a threadarr=("32")
for size in "${cachesizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
