#!/bin/bash
set -x

#Compile the kernel
cd kernel/linux-nova
sudo cp novaconfig .config

#Compile the kernel with '-j' (denotes parallelism) in sudo mode
sudo make -j$PARA
sudo make modules
sudo make modules_install
sudo make install

y="5.1.0"
	if [[ x$ == x ]];
	then
		echo You have to say a version!
		exit 1
	fi

sudo cp ./arch/x86/boot/bzImage /boot/vmlinuz-$y
sudo cp System.map /boot/System.map-$y
sudo cp .config /boot/config-$y
sudo update-initramfs -c -k $y
