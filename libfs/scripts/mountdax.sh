sudo umount /mnt/ram
sudo mkfs -t ext4 /dev/pmem0
sudo mount -o dax /dev/pmem0 /mnt/pmemdir/
sudo chown -R $USER /mnt/pmemdir

