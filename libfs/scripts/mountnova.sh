modprobe nova
sudo mount -t NOVA -o init /dev/pmem1 /mnt/pmemdir
sudo chown -R $USER /mnt/pmemdir

