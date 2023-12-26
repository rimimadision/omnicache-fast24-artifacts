#!/bin/bash

set -x

cur_dir=`readlink -f ./`
ycsb_src=${cur_dir}/ycsb-src

git clone https://github.com/brianfrankcooper/YCSB ${ycsb_src}

cd ${ycsb_src}

mvn install -DskipTests

cd ${cur_dir}
