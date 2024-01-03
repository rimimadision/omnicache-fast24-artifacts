# OmniCache: Collaborative Caching for Near-storage Accelerators

This repository contains the artifact for reproducing our FAST '24 paper "OmniCache: Collaborative Caching for Near-storage Accelerators".

# Table of Contents
* [Overview](#overview)
* [Setup](#setup)
* [Running experiments](#running-experiments)
* [Known Issues](#known-issues)


# Overview 

### Directory structure

    ├── libfs                          # Userspace library (LibFS)
    ├── libfs/scripts                  # Scripts to mount NearStorageFS and microbenchmark scripts
    ├── libfs/benchmark                # Microbenchmark executables
    ├── kernel/linux-4.15.0            # Linux kernel
    ├── kernel/linux-4.15.0/fs/crfs    # Emulated device firmware file system (NearStorageFS)
    ├── appbench                       # Application workloads
    ├── LICENSE
    └── README.md

### Environment: 

Our artifact is based on **Linux kernel 4.15.18** and it should run on any Linux distribution. The current scripts are developed for **Ubuntu 18.04.5 LTS**. Porting to other Linux distributions would require some script modifications.

Our artifact requires a machine **equipped with Intel Optane persistent memory**. To enable outside access to Optane servers, we are using a different machine with a slightly lower memory capacity.  

# Setup 

**NOTE: If you are using our provided machine for AE, we have cloned the code and installed the kernel for you. The repo path is `/localhome/aereview`, you can directly go to Step 4.**

### 1. Get OmniCache source code on Github

```
$ cd /localhome/aereview
$ git clone https://github.com/RutgersCSSystems/omnicache-fast24-artifacts
```

### 2. Install required libraries for OmniCache

```
$ cd omnicache-fast24-artifacts
$ ./deps.sh # If you are using our provided machine for AE, then you don't need to reinstall the dependency.
```
NOTE: If you are prompted during Ubuntu package installation, please hit enter and all the package installation to complete.

### 3. Compile the kernel first

First, we need to install our modified kernel and then reboot.

NOTE:  If you are using our provided machine for AE, we have installed the kernel for you. You don't need to reinstall the kernel. 

```
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ ./scripts/compile_kernel.sh
$ Open /boot/grub.cfs in sudo mode
$ Select the kernel 4.15.18 as the default kernel in the boot and replace the following line
'linux   /boot/vmlinuz-4.15.18 root=UUID=cbcd0ffe-978a-11e9-9a6b-0cc47afdfd54 ro scsi_mod.use_blk_mq=1 maybe-ubiquity'
to
'linux   /boot/vmlinuz-4.15.18 root=UUID=cbcd0ffe-978a-11e9-9a6b-0cc47afdfd54 ro scsi_mod.use_blk_mq=1 maybe-ubiquity memmap=80G$80G'
$ Save the changes to grub
$ sudo reboot
$ After reboot, check the kernel version. It should be 4.15.18
```

### 4. Set environmental variables and compile and install libraries

Please use `screen` to manage the terminal session and maintain the connection.

```
$ screen
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ cd $LIBFS
$ source scripts/setvars.sh
$ make && make install
```

**Please note, if you get logged out of the SSH session or reboot the system (as mentioned below), you must repeat step 4 and set the environmental variable again before running.** 

### 5. Mount NearStorageFS

First, check where the storage is mounted
```
$ ls /dev/pmem*
```
The output could be /dev/pmem0 or /dev/pmem1

Then mount the near-storage FS to either /dev/pmem0 or /dev/pmem1
```
$ cd $BASE/libfs
$ ./scripts/mount_nearstoragefs.sh pmem0
or
$ ./scripts/mount_nearstoragefs.sh pmem1
```

If successful, you will find a NearStorageFS (device-level FS) mounted as follows after executing the `lsblk`

```
pmem0       259:3    0 248.1G  0 disk /mnt/ram
```

# Running Experiments:

### Run micro-bench 

Assume the current directory is the project root directory.

```
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ cd $BASE/libfs/benchmark/
$ mkdir build
$ make
```

#### 0. Run a "Hello world" example for kick-the-tires:

```
$ cd $BASE/libfs/benchmark/
$ ./scripts/run_omnicache_quick.sh
```

Expect output will be similar to ```Benchmark takes 0.97 s, average thruput 4.45 GB/s```. If you can see the above output, you are good for all necessary environmental settings. You can start running all other experiments for artifact evaluation.

#### 1. Run (**Figure 4**):

For sequential access (Figure 4a and Figure 4b)

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure4
$ ./figure4-seq.sh
$ python3 results-extract-seq.py
$ cat RESULT-seq.csv
```

For random access (Figure 4c and Figure 4d)

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure4
$ ./figure4-rand.sh
$ python3 results-extract-rand.py
$ cat RESULT-rand.csv
```

#### 2. Run (**Figure 6**):

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure6
$ ./figure6.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

#### 3. Run (**Figure 7**):

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure7
$ ./figure7.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

#### 4. Run (**Figure 8**):

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure8
$ ./figure8.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

#### 5. Run (**Figure 11**):


```
$ cd $BASE/libfs/benchmark/ae_scripts/figure11
$ ./figure11.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

#### 6. Reboot system (optional): 

The system may require occasional restarts because of a compatibility issue between our motherboard and Optane hardware, which makes Optane responsive. To reboot, we recommend using our hardware reboot emergency script instead of the traditional sudo reboot. To use our emergency script,
```
# Navigate to the artifact's root folder
$ cd /localhome/aereview/omnicache-fast24-artifacts
$ sudo scripts/emergency.sh   
```

### Run LevelDB
Assume the current directory is the project root directory.

#### 1. Compile SHIM Library

SHIM library intercepts POSIX I/O operations and directs them to the OmniCache library for converting them to NearStorageFS commands.

```
$ cd $BASE/libfs/libshim
$ ./makeshim.sh
```

#### 2. Compile LevelDB

```
$ cd $BASE/appbench/leveldb/ae_scripts/
$ ./make_db_bench.sh
```

#### 3. Run YCSB

```
$ cd $BASE/appbench/leveldb/ae_scripts/figure12a
$ ./figure12a.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

#### 4. Run LevelDB dbbench

```
$ cd $BASE/appbench/leveldb/ae_scripts/figure13
$ ./figure13.sh
$ python3 results-extract.py
$ cat RESULT.csv
```

# Known issues 
1. The system may require occasional restarts because of a compatibility issue between our motherboard and Optane0, which makes Optane irresponsive. To reboot, we recommend using our hardware reboot emergency script instead of the traditional sudo reboot. To use our emergency script,
```
# Navigate to the artifact's root folder
$ cd /localhome/aereview/omnicache-fast24-artifacts
$ sudo scripts/emergency.sh   
```
After rebooting, as mentioned in step 4 above, make sure to set the environmental variable again.

3. LevelDB may occasionally hang after dbbench finishes. Simply pressing Ctrl-c to kill the process can solve the issue.
