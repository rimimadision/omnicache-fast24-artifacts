#ifndef DEVFSLIBIO_H
#define DEVFSLIBIO_H

#include <sys/stat.h>

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <uthash.h>
#include <interval_tree.h>
//#include <mm.h>
#include "list.h"
#include "utils.h"

#ifdef COMPRESS_CACHE
#include "snappy.h"
#endif

#include "unvme_nvme.h"

#define MAX_OPEN_FILE 1048576
#define MAX_THREAD_NR  1048576
#define MAX_OPEN_INODE 1024
#define MAX_FP_PER_INODE 512
#define SHADOW_FD_NR  32

#define INVALID_SLBA -1
#define FD_CONFLICT_FACTOR 13

#define DEVFS_CMD_FINISH 1
#define DEVFS_CMD_READY  2
#define DEVFS_CMD_BUSY   4

#define FD_QUEUE_PAGE_NUM 1
#define FD_QUEUE_POOL_PG_NUM 512

#define CXL_MEM_PG_NUM 2048
#define CXL_MEM_PG_SIZE (2*1024*1024UL)
//#define CXL_MEM_PG_SIZE 4096

#define CID 1
#define NSID 1
#define FILECLOSED 9

#define DEVFS_SUBMISSION_TREE_FOUND 0
#define DEVFS_SUBMISSION_TREE_NOTFOUND 1

#define INVALID_CRED 0xFF
#define CRED_ID_BYTES 16

#define SHM_ADDR 0x00007f0000000000
#define SHM_SIZE 1024*1024*32
#define SHM_POOL "/dev/shm/shmpoll"
#define SHM_FILE "/mnt/tmpfs/shm"

#define VIR_TIME_CLOCE 1
#define VIR_TIME_READ 1
#define VIR_TIME_APPEND 1
#define VIR_TIME_WRITE 1
#define VIR_TIME_MATCH 1
#define VIR_TIME_LEVELDB_LOG_CHKSM 3
#define VIR_TIME_CHKSM 3
#define VIR_TIME_READ_CHKSM 5
#define VIR_TIME_APPEND_CHKSM 5
#define VIR_TIME_WRITE_CHKSM 5
#define VIR_TIME_COMPRESS_WRITE 5
#define VIR_TIME_READ_MODIFY_WRITE 5
#define VIR_TIME_READ_APPEND 5
#define VIR_TIME_OPEN_WRITE_CLOSE 5

#define NODE_CMD_SIZE 1024

//#define DEBUG
#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__ )
#else
#define debug_printf(...) do{ }while(0)
#endif

/* FD-queue mem pool */
struct fd_q_mem {
	void *mem;
	int bitmap[FD_QUEUE_POOL_PG_NUM];
	int head;
	crfs_mutex_t lock;
};

/* FD-queue */
typedef struct fd_q {
	void *vsq;
	int sq_head;
	int size;
} fd_q;

/* Declare user-level file pointer */
struct ufile;

static int stat_interval = 10;

struct profiling_stat {
        int op_host_count;
        int op_device_count;

        double total_device_exec_time;
        double total_host_exec_time;

        double avg_device_exec_time;  
        double avg_host_exec_time;  

        double bw_dm_to_hm;  
        double bw_ds_to_hm;  

        double queue_len;  
};

/* User-level inode */
typedef struct uinode {
	struct ufile *ufilp[MAX_FP_PER_INODE];
#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        unsigned int open_ref;
#endif
	char fname[256];
	int ref;
	int fsync_barrier;
	int fsync_counter;
        int cache_tree_init;
        //pthread_rwlock_t       rwlock;
        crfs_rwlock_t cache_tree_lock;
        struct rb_root cache_tree;
        struct list_head open_node;
        struct list_head close_node;
	UT_hash_handle hh;	/* makes this structure hashable */
        struct cxl_mem_namespace* cxl_mem_ns;

} uinode;

/* User-level file pointer */
typedef struct ufile {
	int fd;
	int ref;
	off_t off;
        off_t flush_off;
	fd_q fd_queue;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	char fname[256];
	int perm;
	mode_t mode;
	pid_t tid;
#ifdef SHADOW_FD
	int shadow_fd[SHADOW_FD_NR];
	int shadow_fd_nr;
#endif
	int closed;
	int closed_conflict;

	uinode *inode;
	int inode_idx;
    struct profiling_stat stat;
} ufile;

/* Open file table */
struct open_file_table {
	ufile open_files[MAX_OPEN_FILE];
	int tid_to_fd[MAX_THREAD_NR];
};

/* Inode table */
extern uinode *inode_table;
extern crfs_mutex_t uinode_table_lock;

//#ifdef PARAFS_INTERVAL_TREE
/* Per-inode interval tree */
struct req_tree_entry {
    void *cxl_blk_addr;
    void *blk_addr;
    nvme_command_rw_t cmds[NODE_CMD_SIZE];
    int cmd_size;
    int data_size;
    int size;
    int cache_in_kernel;
    struct ufile *fp;
    struct list_head lru_node;
    struct interval_tree_node it;
    crfs_mutex_t lock;

    size_t cxl_buf_start;
    size_t cxl_buf_end;

    size_t buf_start;
    size_t buf_end;

    int dirty;
    int in_lru_list;
};
//#endif

/* Compound Operation I/O vec */
struct macro_io_vec {
        int opc;
        uint64_t prp;
        uint64_t addr;
        uint64_t slba;
        uint64_t nlb;
};

struct macro_op_desc {
        uint16_t num_op;
        struct macro_io_vec iov[16];
};


extern unsigned int qentrycount;
extern unsigned int schedpolicy;
extern unsigned int devcorecnt;
extern int isjourn;

int initialize_crfs(unsigned int qentry_count,
		unsigned int dev_core_cnt, unsigned int sched_policy, unsigned int cxl_ns_size);
int shutdown_crfs(void);
size_t crfs_read(int fd, void *p, size_t count);
size_t crfs_write(int fd, const void *p, size_t count);
size_t crfs_pread(int fd, void *p, size_t count, off_t offset);
size_t crfs_pwrite(int fd, const void *p, size_t count, off_t offset);
int crfs_lseek64(int fd, off_t offset, int whence);
int crfs_open_file(const char *fname, int perm, mode_t mode);
int crfs_close_file(int fd);
int crfs_fsync(int fd);
int crfs_fallocate(int fd, off_t offset, off_t len);
int crfs_ftruncate(int fd, off_t length);
int crfs_unlink(const char *pathname);
int crfs_rename(const char *oldpath, const char *newpath);

/* Compound POSIX calls (containing computing along with I/O command) */
size_t devfs_readmodifywrite(int fd, const void *p, size_t count, off_t offset);
size_t devfs_readmodifywrite_batch(int fd, const void **p, size_t count, off_t* offsets, size_t batch_size);
size_t devfs_open_pwrite_close_batch(int fd, const void **p, size_t count, off_t* offset, char* out_file, size_t batch_size);
size_t devfs_readmodifyappend(int fd, const void *p, size_t count);
size_t devfs_checksumread(int fd, void *p, size_t count, uint8_t crc_pos);
size_t devfs_checksumwrite(int fd, const void *p, size_t count, uint8_t crc_pos);
size_t devfs_checksumpwrite_batch(int fd, const void **p, size_t count, 
                off_t* offset, uint8_t crc_pos, size_t batch_size);
/* Cache FusionOps */
size_t devfs_checksumpwrite_cache_host(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_appendchksmpwrite_cache_hybrid(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_appendchksmpwrite_cache_kernel(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);

size_t devfs_readchksmpwrite(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_readchksmpwrite_cache_host(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_readchksmpwrite_cache_kernel(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_readchksmpwrite_cache_hybrid(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);

size_t devfs_checksumpread(int fd, void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_checksumpwrite(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos);
size_t devfs_open_pwrite_close(int fd, const void *p, size_t count, off_t offset, char* out_file);
size_t devfs_open_pread_close(int fd, const void *p, size_t count, off_t offset, char* filename);
ssize_t devfs_read_append(int fd, const void *buf, size_t count);

size_t devfs_compresswrite(int fd, const void *p, size_t count, char* in_file);
size_t devfs_compresswrite_cache(int fd, const void *p, size_t count, char* in_file);

size_t devfs_read_knn_write(int fd, const void *p, size_t count, off_t offset);

/* Encryption related Compound POSIX Calls */
size_t devfs_encryptwrite(int fd, const void *buf, size_t count);
size_t devfs_encryptpwrite(int fd, const void *buf, size_t count, off_t offset);
size_t devfs_preadencryptpwrite(int fd, const void *buf, size_t count, off_t offset);

/* Leveldb related Compound POSIX Calls */
size_t leveldb_checksumwrite(int fd, const void* data, size_t data_len, char* meta,
        size_t meta_len, int checksum_pos, int type_pos, uint8_t end, int cal_type, uint32_t type_crc);

ssize_t leveldb_checksumread(int fd, void *buf, size_t count, uint8_t end);

ssize_t leveldb_checksumpread(int fd, void *buf, size_t count, off_t offset, uint8_t end);

void fault_handler(int signo, siginfo_t *info, void *extra);
void setHandler(void (*handler)(int,siginfo_t *,void *));

nvme_command_rw_t* gen_io_cmd(ufile *fp, void* buf, int opc, u64 slba, u64 nlb);

int dev_cache_rw(struct req_tree_entry *node, ufile *fp, void* buf, unsigned long start, unsigned long end, int rw);

int vfio_crfs_queue_write(int dev, int fd, void *vsq, int vsqlen);
void vfio_put_fd_queue_buffer(void *q_addr);
int crfs_write_submission_tree_delete(uinode *inode, nvme_command_rw_t *cmdrw);
void put_inode(uinode *inode, ufile *fp);
int vfio_crfs_close_file(int dev, int fd);

int unvme_do_devfs_io_macro(int dev, int dfd, void *ioqq, 
	int opc, void* buf, u64 slba, u64 nlb, uint8_t crc_pos,
	struct macro_op_desc *desc, void* blk_addr);


void* find_avail_vsq_entry(ufile *fp);

int nvme_cmd_rw_new(int opc, u16 cid, int nsid, u64 slba, u64 nlb, u64 prp2,
		void *vsq);


/* NVM related clflush and mfence */
#define CACHE_LINE_SIZE 64
#define ASMFLUSH(dest) __asm__ __volatile__ ("clflush %0" : : "m"(*(volatile char *)dest))

static inline void clflush(volatile char* __p) {
	asm volatile("clflush %0" : "+m" (*__p));
	return;
}

static inline void mfence() {
	asm volatile("mfence":::"memory");
	return;
}

static void flush_cache(void *ptr, size_t size) {
	unsigned int  i=0;  mfence();
	for (i = 0; i < size; i = i + CACHE_LINE_SIZE) {
		clflush((volatile char*)ptr);
		ptr += CACHE_LINE_SIZE;
	}
	mfence();
	return;
}

/* Hashing functions for LibFS inode table */
static void uinode_table_insert(uinode *inode) {
	HASH_ADD_STR(inode_table, fname, inode);
}

static uinode* uinode_table_lookup(const char* fname) {
	uinode *inode;
	HASH_FIND_STR(inode_table, fname, inode);
	return inode;
}

static void uinode_table_delete(uinode *inode) {
	HASH_DEL(inode_table, inode);
}

/* Statistical data on queue hit rate and conflict rate */
extern int fp_queue_access_cnt;
extern int fp_queue_hit_cnt;
extern int fp_queue_conflict_cnt;

/*
 * File pointer queue hit stat
 */
static inline void crfs_stat_fp_queue_init() {
	__sync_lock_test_and_set(&fp_queue_access_cnt, 0);
	__sync_lock_test_and_set(&fp_queue_hit_cnt, 0);
	__sync_lock_test_and_set(&fp_queue_conflict_cnt, 0);
}

static inline void crfs_stat_fp_queue_access() {
	__sync_fetch_and_add(&fp_queue_access_cnt, 1);
}

static inline void crfs_stat_fp_queue_hit() {
	__sync_fetch_and_add(&fp_queue_hit_cnt, 1);
}

static inline void crfs_stat_fp_queue_conflict() {
	__sync_fetch_and_add(&fp_queue_conflict_cnt, 1);
}

static inline void crfs_stat_fp_queue_count() {
        printf("queue access count = %d\n", fp_queue_access_cnt);
        printf("queue hit count = %d\n", fp_queue_hit_cnt);
        printf("queue conflict count = %d\n", fp_queue_conflict_cnt);
        crfs_stat_fp_queue_init();
}

size_t crfs_checksumwritecc(int fd, const void *p, size_t count, int crc_pos);

/* Inject Crash calls */
int crfs_injectcrash(int fd, int crash_code);

#endif
