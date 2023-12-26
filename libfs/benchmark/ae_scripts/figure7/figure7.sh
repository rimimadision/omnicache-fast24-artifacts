export AERESULTDIR=$AERESULTS/microbench/figure7/fusionfs
$MICROBENCH/ae_scripts/scripts/cache_size/run_posix_io_fusionfs.sh

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure7/hostcache
$MICROBENCH/ae_scripts/scripts/cache_size/run_posix_io_host_cache.sh

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure7/omnicache
$MICROBENCH/ae_scripts/scripts/cache_size/run_posix_io_omnicache.sh



