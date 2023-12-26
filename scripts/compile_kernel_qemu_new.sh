#! /bin/bash

PARA=16
VER=4.15.18

set -x

#cd kernel/kbuild

#make -f Makefile.setup .config
#make -f Makefile.setup
#make -j$PARA
#sudo make modules
#sudo make modules_install
#sudo make install

cd kernel/linux-4.15.0
sudo cp def.config .config
sudo make -j$PARA 

sudo cp ./arch/x86/boot/bzImage $KERNEL/vmlinuz-$VER
sudo cp System.map $KERNEL/System.map-$VER

set +x
