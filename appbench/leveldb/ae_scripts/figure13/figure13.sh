export AERESULTDIR=$AERESULTS/leveldb/figure13/fusionfs
$LEVELDB/ae_scripts/scripts/run_leveldb_fusionfs.sh

echo "COOLING OFF FOR $1"
sleep $1
$BASE/libfs/crfsexit

export AERESULTDIR=$AERESULTS/leveldb/figure13/hostcache
$LEVELDB/ae_scripts/scripts/run_leveldb_hostcache.sh

echo "COOLING OFF FOR $1"
sleep $1
$BASE/libfs/crfsexit

export AERESULTDIR=$AERESULTS/leveldb/figure13/omnicache
$LEVELDB/ae_scripts/scripts/run_leveldb_omnicache.sh

echo "COOLING OFF FOR $1"
sleep $1
$BASE/libfs/crfsexit
