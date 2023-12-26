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

Our artifact requires a machine **equipped with Intel Optane persistent memory**.

# Setup 

**NOTE: If you are using our provided machine for AE, we have cloned the code and installed the kernel for you. The repo path is `/localhome/aereview`, you can directly goto Step 4.**

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

### 3. Compile kenrel 

First, we need to install our modified kernel and then reboot.

NOTE:  If you are using our provided machine for AE, we have installed the kernel for you. You don't need to reinstall the kernel. 

```
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ ./scripts/compile_kernel.sh
$ sudo reboot

```

### 4. Set environmental variables and compile and install libraries

Please use `screen` to manage the terminal session for maintaining the connection.

```
$ screen
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ cd $LIBFS
$ source scripts/setvars.sh
$ make && make install
```

### 5. Mount NearStorageFS

```
$ cd $BASE/libfs
$ ./scripts/mount_nearstoragefs.sh
```

If successful, you will find a NearStorageFS (device-level FS) mounted as follows after executing the `lsblk`

```
pmem1       259:3    0 248.1G  0 disk /mnt/ram
```

# Running Experiments:

### Run micro-bench 

Assume current directory is the project root directory.

```
$ cd omnicache-fast24-artifacts
$ source scripts/setvars.sh
$ cd $BASE/libfs/benchmark/
$ make
```

#### 0. Run a "Hello world" example for kick-the-tires:

```
$ cd $BASE/libfs/benchmark/
$ ./scripts/run_omnicache_quick.sh
```

Expect output will be similar to ```Benchmark takes 0.97 s, average thruput 4.45 GB/s```. If you can see the above output, then you are good for all necessary environmental settings. You can start running all other experiments for artifact evaluation.

#### 1. Run (**Figure 4**):

```
$ cd $BASE/libfs/benchmark/ae_scripts/figure4
$ ./figure4.sh
$ python3 results-extract.py
$ cat RESULT.csv
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


### Run LevelDB

Assume current directory is the project root directory.

#### 1. Compile SHIM Library

SHIM library is responsible for intercepting POSIX I/O operations and directing them to the OmniCache library for converting them to NearStorageFS commands.

```
$ cd $BASE/libfs/libshim
$ ./makeshim.sh
```

#### 2. Compile LevelDB

```
$ cd $BASE/appbench/leveldb/ae_scripts/
$ ./make_db_bench.sh
```

#### 3. Run LevelDB dbbench

```
$ cd $BASE/appbench/leveldb/ae_scripts/figure13
$ ./figure13.sh
$ python3 results-extract.py
```

### YCSB

#### 1. Compile YCSB

YCSB: Compiling YCSB requires installing JDK 8 as well as installing maven version 3. Please follow the steps below:

```
$ sudo add-apt-repository ppa:openjdk-r/ppa
$ sudo apt update
$ sudo apt install openjdk-8-jdk
$ export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
$ export PATH=$PATH:$JAVA_HOME/bin
Check installation using java -version
$ sudo apt install maven
```

#### 2. Run YCSB
```
$ source scripts/setvars.sh
$ cd $BASE/appbench/leveldb/ae_scripts/figure12a
$ ./run_ycsb.sh
$ python3 results-extract.py
```

# Known issues 

1. The system may requires occasional restart because of some Optane compatibility issues

2. LevelDB may occasionally hang after dbbench finishes. Simply pressing Ctrl-c to kill the process can solve the issue.
