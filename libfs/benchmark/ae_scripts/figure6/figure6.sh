export AERESULTDIR=$AERESULTS/microbench/figure6/fusionfs
$MICROBENCH/ae_scripts/scripts/io_size/run_posix_io_fusionfs.sh
$LIBFS/crfsexit
sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure6/hostcache
$MICROBENCH/ae_scripts/scripts/io_size/run_posix_io_host_cache.sh
$LIBFS/crfsexit
sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure6/omnicache
$MICROBENCH/ae_scripts/scripts/io_size/run_posix_io_omnicache.sh
$LIBFS/crfsexit


