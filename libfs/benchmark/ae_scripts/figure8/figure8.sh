set -x

export AERESULTDIR=$AERESULTS/microbench/figure8/fusionfs
$MICROBENCH/ae_scripts/scripts/run_cisc_io_fusionfs.sh
$LIBFS/crfsexit

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure8/hostcache
$MICROBENCH/ae_scripts/scripts/run_cisc_io_host_cache.sh
$LIBFS/crfsexit
sleep 5


export AERESULTDIR=$AERESULTS/microbench/figure8/omnicache
$MICROBENCH/ae_scripts/scripts/run_cisc_io_omnicache.sh
$LIBFS/crfsexit
