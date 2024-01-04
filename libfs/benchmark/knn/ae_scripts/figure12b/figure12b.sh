set -x


export AERESULTDIR=$AERESULTS/knn/figure12b/fusionfs
$MICROBENCH/knn/ae_scripts/scripts/run_knn_fusionfs.sh

sleep 5

export AERESULTDIR=$AERESULTS/knn/figure12b/hostcache
$MICROBENCH/knn/ae_scripts/scripts/run_knn_hostcache.sh

sleep 5

export AERESULTDIR=$AERESULTS/knn/figure12b/omnicache
$MICROBENCH/knn/ae_scripts/scripts/run_knn_omnicache.sh
$LIBFS/crfsexit
