set -x

export AERESULTDIR=$AERESULTS/microbench/figure11/fusionfs
$MICROBENCH/ae_scripts/scripts/run_posix_io_fusionfs.sh

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure11/hostcache
$MICROBENCH/ae_scripts/scripts/run_posix_io_host_cache.sh

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure11/omnicache
$MICROBENCH/ae_scripts/scripts/run_posix_omnicache.sh

sleep 5

export AERESULTDIR=$AERESULTS/microbench/figure11/omnicachecxl
$MICROBENCH/ae_scripts/scripts/run_posix_io_omnicache_cxl.sh





