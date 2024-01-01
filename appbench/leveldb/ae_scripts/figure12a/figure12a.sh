export AERESULTDIR=$AERESULTS/ycsb/fusionfs
$LEVELDB/ae_scripts/scripts/run_ycsb_fusionfs.sh

sleep 5
#$BASE/libfs/crfsexit

export AERESULTDIR=$AERESULTS/ycsb/hostcache
$LEVELDB/ae_scripts/scripts/run_ycsb_hostcache.sh

sleep 5
#$BASE/libfs/crfsexit

export AERESULTDIR=$AERESULTS/ycsb/omnicache
$LEVELDB/ae_scripts/scripts/run_ycsb_omnicache.sh

sleep 5
$BASE/libfs/crfsexit
