cd $LEVELDB

make db_bench_fusionfs -j32
make db_bench_hostcache -j32
make db_bench_omnicache -j32
