set -x

export AERESULTDIR=$AERESULTS/microbench/figure4/fusionfs
$MICROBENCH/ae_scripts/scripts/run_posix_io_fusionfs.sh

sleep 5

$LIBFS/crfsexit

export AERESULTDIR=$AERESULTS/microbench/figure4/hostcache
$MICROBENCH/ae_scripts/scripts/run_posix_io_host_cache.sh
$LIBFS/crfsexit

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure4/lambdaio-emulate
$MICROBENCH/ae_scripts/scripts/run_posix_io_lambdaio_cache.sh
$LIBFS/crfsexit
sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure4/omnicache
$MICROBENCH/ae_scripts/scripts/run_posix_io_omnicache.sh
$LIBFS/crfsexit




