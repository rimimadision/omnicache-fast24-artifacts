#!/bin/bash

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
setup_dir=`readlink -f ../configs`
pmem_dir=/mnt/ram

run_ycsb()
{
    fs=$1
    script="./run_${fs}.sh" 
    for run in 1
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-7 $script LoadA $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunA $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunB $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunC $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunF $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunD $fs $run
        sleep 5
        sudo taskset -c 0-7 $script LoadE $fs $run
        sleep 5
        sudo taskset -c 0-7 $script RunE $fs $run
        sleep 5
    done
}

run_ycsb fusionfs 

run_ycsb hostcache 

run_ycsb omnicache

