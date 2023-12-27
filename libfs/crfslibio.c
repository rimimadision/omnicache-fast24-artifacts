#define _GNU_SOURCE


#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <linux/pci.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include "unvme_nvme.h"
#include "vfio.h"
#include "./crc32/crc32_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "cache.h"
#include "vect_cmd.h"
#include "crfslibio.h"
#include "time_delay.h"
#include "utils.h"
#include "cxl_mem.h"
#include "knn.h"

#ifdef _NVMFDQ
#include "nvmlog/pvmobj/nv_def.h"
#include "nvmlog/pvmobj/nv_map.h"
#include "nvmlog/pvmobj/cache_flush.h"
#include "nvmlog/pvmobj/c_io.h"
#endif

#define OUTPUT_DIR "/mnt/ram/output_dir/"
#define PAGE_SIZE 4096
#define BLOCKSIZE 512
#define FILENAME "/mnt/ram/devfile9"
#define TEST "/mnt/ram/test"
#define OPSCNT 100000
#define CREATDIR O_RDWR | O_CREAT | O_TRUNC
#define READIR O_RDWR
//#define _DEBUG

//#define CREATDIR O_RDWR|O_CREAT|O_TRUNC //|O_DIRECT
#define MODE S_IRWXU //S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
#define NUM_QUEUE_CMDS 1
#define CHECKSUMSIZE 4
#define MAGIC_SEED 0

#define FILEPERM 0666
/* Global variables */
static void* vsq;
static size_t cmd_nr = 1;
static int verbose_flag;
static int isread, isdelete;
static int numfs;
char fname[256];

static struct uinode* all_inodes[1024];
static long all_files_cnt = 0;
//DevFS device
static int g_dev;
static size_t g_vsqlen = 1;

#ifdef COMPRESS_CACHE
static int g_snappy_init = 0;
static struct snappy_env g_snappy_env;
#endif

//DevFS device global queue
//void *g_vsq;
struct fd_q_mem fd_q_mempool;

//DevFS global sba
static int g_slba;

/* Configurable parameters */
unsigned int qentrycount = 2;	// Kernel per-fd queue entry count 
unsigned int schedpolicy = 0;	// Device thread scheduler policy
unsigned int devcorecnt = 4;	// Device thread number
unsigned int fdqueuesize = 4096;// FS libray per-fd queue size
int isjourn = 0;
int isexit = 0;
int stopeviction = 0;
int max_op = nvme_cmd_read_knn_write;

/* Per process cred id */
static u8 cred_id[16] = {INVALID_CRED};

/* Statistical data on queue hit rate and conflict rate */
int fp_queue_access_cnt = 0;
int fp_queue_hit_cnt = 0;
int fp_queue_conflict_cnt = 0;

/* Global open file table */
#ifdef PARAFS_SHM
struct open_file_table* ufile_table_ptr;
#define ufile_table (*ufile_table_ptr)
#else
struct open_file_table ufile_table;
#endif

/* Inode table */
uinode *inode_table = NULL;
crfs_mutex_t uinode_table_lock = CRFS_MUTEX_INITIALIZER;

int getargs(int argc, char **argv);

// Statistics
int total_read = 0;
int per_thread_read[MAX_THREAD_NR] = {0};
crfs_mutex_t counter_mutex = CRFS_MUTEX_INITIALIZER;

double simulation_time_us(struct timeval start, struct timeval end) {
        double current_time;
        current_time =  (end.tv_sec - start.tv_sec)*1.0 * 1000000 + (end.tv_usec - start.tv_usec);
        return current_time;
}

//#ifdef _USE_OPT
void fault_handler(int signo, siginfo_t *info, void *extra)
{
        fprintf(stderr, "Signal %d received\n", signo);
	if(isexit != 1)
	        shutdown_crfs();
	exit(-1);
}

void setHandler(void (*handler)(int,siginfo_t *,void *))
{

	//http://man7.org/linux/man-pages/man7/signal.7.html
        struct sigaction action;
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = handler;

        if (sigaction(SIGTERM, &action, NULL) == -1) {
                perror("SIGTERM: sigaction");
                _exit(1);
        }

        if (sigaction(SIGSEGV, &action, NULL) == -1) {
                perror("sigsegv: sigaction");
                _exit(1);
        }
        
}
//#endif


/*
 * clean up file system state and release resource after
 * shutdown_crfs() is called for some applications like
 * RocksDB
 */
int crfs_handle_exit(int fd, nvme_command_rw_t *cmd) {
        int ret = 0;

	if (cmd && cmd->common.opc == nvme_cmd_close) {
		/*
		 * If it's a close command, just call close() directly
		 * The firmfs will release its resource
		 */
		struct vfio_crfs_closefd_cmd map;
		map.argsz = sizeof(map);
		map.fd = fd;

		ret = ioctl(g_dev, VFIO_DEVFS_CLOSEFILE_CMD, &map);
		if (ret < 0) {
			fprintf(stderr,"ioctl VFIO_DEVFS_CLOSEFILE_CMD errno %d \n", errno);
			return -1;
		}
	} else {
		ret =  cmd->nlb;
	}
	return ret;
}

u32 get_vir_time(int opc) {
    switch(opc) {
        case nvme_cmd_close:
            return VIR_TIME_CLOCE;
        case nvme_cmd_read:
            return VIR_TIME_READ;
        case nvme_cmd_append:
            return VIR_TIME_APPEND;
        case nvme_cmd_write:
            return VIR_TIME_WRITE;
        case nvme_cmd_chksm:
            return VIR_TIME_CHKSM;
        case nvme_cmd_match:
            return VIR_TIME_MATCH;
        case nvme_cmd_leveldb_log_chksm:
            return VIR_TIME_LEVELDB_LOG_CHKSM;
        case nvme_cmd_read_chksm:
            return VIR_TIME_READ_CHKSM;
        case nvme_cmd_append_chksm:
            return VIR_TIME_APPEND_CHKSM;
        case nvme_cmd_write_chksm:
            return VIR_TIME_WRITE_CHKSM;
        case nvme_cmd_compress_write:
            return VIR_TIME_COMPRESS_WRITE;
        case nvme_cmd_read_modify_write:
        case nvme_cmd_read_modify_append:
        case nvme_cmd_read_modify_write_batch:
        case nvme_cmd_write_chksm_batch:
            return VIR_TIME_READ_MODIFY_WRITE;
        case nvme_cmd_read_append:
            return VIR_TIME_READ_APPEND;
        case nvme_cmd_open_write_close:
            return VIR_TIME_OPEN_WRITE_CLOSE;
    }
    return 0;
}

#ifndef PARAFS_BYPASS_KERNEL
/*
 *  DevFS write
 */
int vfio_crfs_queue_write(int dev, int fd, void *vsq, int vsqlen) {

	long ret;

#if defined(_DEBUG)
	fprintf(stderr,"******vfio_crfs_queue_write******* %ld \n", ret);
#endif
	if (isexit == 1) {
		/*
		 * For some applications like RocksDB, even after benchmark finishes
		 * and device threads are terminated, the application background
		 * threads keeps doing some I/O operations like close() or append to
		 * the log files. If that is the case, we call crfs_handle_exit()
		 * to call close() directly to release its file system resource.
		 */
		nvme_command_rw_t *cmd = (nvme_command_rw_t*)vsq;
		return cmd->nlb;
	}

#ifdef ASSERT_ENABLE
	assert(vsq != NULL);
	assert(vsqlen > 0);
#endif

	struct vfio_crfs_rw_cmd map = {
			.argsz = sizeof(map),
			.flags = (VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE),
			.vaddr = (__u64)vsq,
			.iova = (__u64)0, //dev->iova,
			.size = (__u64)0,
			.fd = fd,
			.cmd_count = vsqlen,
	};
	ret = ioctl(dev, VFIO_DEVFS_RW_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_RW_CMD errno %d \n", errno);
		return -1;
	}
#if defined(_DEBUG)
	fprintf(stderr,"******vfio_crfs_queue_write******* %ld \n", ret);
#endif
	return ret;
}
#else
/*
 *  DevFS write
 */
int vfio_crfs_queue_write(int dev, int fd, void *vsq, int vsqlen) {

	long ret;

#ifdef ASSERT_ENABLE
	assert(vsq != NULL);
	assert(vsqlen > 0);
#endif

#ifdef PCIE_DELAY
	emulate_pcie_latency();
#endif

#ifdef FLUSH_CACHE
	flush_cache(vsq, sizeof(nvme_command_rw_t));
#endif

	nvme_command_rw_t *cmd = (nvme_command_rw_t*)vsq;

	if (isexit == 1) {
		/*
		 * For some applications like RocksDB, even after benchmark finishes
		 * and device threads are terminated, the application background
		 * threads keeps doing some I/O operations like close() or append to
		 * the log files. If that is the case, we call crfs_handle_exit()
		 * to call close() directly to release its file system resource.
		 */
		return crfs_handle_exit(fd, cmd);
	}

#if defined(_DEBUG)
	fprintf(stderr,"******vfio_crfs_queue_write begin******* %ld \n", ret);
#endif

#ifdef _USE_OPT
	while(cmd->status != (DEVFS_CMD_READY | DEVFS_CMD_FINISH | DEVFS_CMD_BUSY)) {
#else
	// Set cmd as ready
    __sync_lock_test_and_set(&cmd->status, DEVFS_CMD_READY | DEVFS_CMD_BUSY);
	/* cmd->status |= DEVFS_CMD_READY; */

	// Spin until the command is handled by device;
	while(__sync_fetch_and_add(&cmd->status, 0) != (DEVFS_CMD_READY | DEVFS_CMD_FINISH | DEVFS_CMD_BUSY)) {
#endif
		/*
		 * If this command is getting called after device thread is terminated in some
		 * applications like RocksDB, just break without spin as at this time, device
		 * thread has already terminated
		 */
		if (isexit == 1) {
			cmd->ret = cmd->nlb;
			break;
		}
	}
	ret = cmd->ret;

	cmd->status = 0;

#if defined(_DEBUG)
	fprintf(stderr,"******vfio_crfs_queue_write end******* %ld \n", ret);
#endif

	return ret;
}
#endif


/**
 * NVMe submit a read write command.
 * @param   ioq         io queue
 * @param   opc         op code
 * @param   cid         command id
 * @param   nsid        namespace
 * @param   slba        startling logical block address
 * @param   nlb         number of logical blocks
 * @param   prp1        PRP1 address
 * @param   prp2        PRP2 address
 * @return  0 if ok else -1.
 */
int nvme_cmd_rw_new(int opc, u16 cid, int nsid, u64 slba, u64 nlb, u64 prp2, 
		void *vsq)
{
	nvme_command_rw_t *cmd = (nvme_command_rw_t*)vsq;
#ifndef _USE_OPT
	//memset(cmd, 0, sizeof(nvme_command_rw_t));
#endif
	cmd->common.opc = opc;
	cmd->common.cid = cid;
	/* cmd->common.nsid = nsid; */

	cmd->common.prp2 = prp2;
	cmd->slba = slba;
	cmd->nlb = nlb;
	cmd->kalloc = 0;
    cmd->vir_runtime = get_vir_time(opc);
    memcpy(cmd->cred_id, cred_id, CRED_ID_BYTES);

#ifdef _USE_OPT
	cmd->status = DEVFS_CMD_READY;
#endif 

#if defined(_DEBUG)
	printf("cmd->slba %llu cmd->nlb %llu\n", cmd->slba, cmd->nlb);
#endif
}


/*
 * User-level inode get and put function
 */
static uinode* get_inode(const char *fname, ufile *fp) {
	int i = 0;
	uinode *target = NULL;

	// Need mutex lock wrap around here
	crfs_mutex_lock(&uinode_table_lock);

	target = uinode_table_lookup(fname);
	if (!target) {
		target = (uinode*)crfs_malloc(sizeof(uinode));
		memset(target, 0, sizeof(uinode));
                strcpy(target->fname, fname);
		uinode_table_insert(target);

#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
#ifndef LEVELDB_CACHE
        /* printf("add inode, fname: %s\n", fname); */
        all_inodes[all_files_cnt++] = target;
#endif
#endif

#ifdef CXL_MEM
#ifdef CXL_MEM_NAMESPACE
        target->cxl_mem_ns = cxl_ns_malloc();
#endif
#endif
    }

	if (target) {
                if (target->cache_tree_init == 0) {
                        target->cache_tree_init = 1;
                        //pthread_rwlock_init(&target->rwlock, NULL);
                        crfs_rwlock_init(&target->cache_tree_lock);

                }
		fp->inode = target;
#if defined CRFS_WB_CACHE && !defined LEVELDB_CACHE
                fp->inode_idx = target->open_ref;
                target->open_ref++;
                if(target->open_ref == 1 && target->ref>0) {
                        remove_from_lru_list(&target->close_node, &lru_closed_files_list_lock);
                        /* printf("delete in the close list, fname: %s\n", fp->fname); */
                }

                /* printf("inode open, fname: %s, open_ref: %d, ref: %d\n", fp->fname, target->open_ref, target->ref); */
#else
                fp->inode_idx = target->ref;
#endif
		target->ufilp[target->ref] = fp;
		target->ref++;
	}
	crfs_mutex_unlock(&uinode_table_lock);

	return target;
}

#if defined CRFS_WB_CACHE && !defined LEVELDB_CACHE
void put_inode(uinode *inode, ufile *fp) {
        // Need mutex lock wrap around here
        fp = NULL;
        //printf("put inode\n");
        crfs_mutex_lock(&uinode_table_lock);
        for (int i = 0; i < MAX_FP_PER_INODE; i++) {
                fp = inode->ufilp[i];
                if (fp == NULL) continue;

                inode->ufilp[i] = NULL;
                /* Free fd-queue buffer */
                vfio_crfs_close_file(0, fp->fd);
                vfio_put_fd_queue_buffer(fp->fd_queue.vsq);

                fp->fd = -1;
                fp->ref = 0;
                fp->off = -1;

        }
#if 0
#ifdef CXL_MEM
        cxl_ns_free(inode->cxl_mem_ns);
#endif
#endif

        /* uinode_table_delete(inode); */
        crfs_free(inode);

        crfs_mutex_unlock(&uinode_table_lock);
}
#else
void put_inode(uinode *inode, ufile *fp) {
	// Need mutex lock wrap around here
	crfs_mutex_lock(&uinode_table_lock);
	inode->ufilp[fp->inode_idx] = NULL;
	inode->ref--;

	if (inode->ref == 0) {
		uinode_table_delete(inode);
		crfs_free(inode);
	}

	crfs_mutex_unlock(&uinode_table_lock);
}
#endif

/*
 * Per FD-queue find and release entry function
 */
#ifndef PARAFS_BYPASS_KERNEL
static void* find_avail_vsq_entry(ufile *fp) {
	int i = 0;
	fd_q *fd_queue = (fd_q*)&fp->fd_queue;
	void *ret = NULL;
	void *vsq = fd_queue->vsq;
	nvme_command_rw_t *cmd = NULL;

	cmd = (nvme_command_rw_t*)(fd_queue->vsq + fd_queue->sq_head);
	if (cmd->status == 0) {
		cmd->status = DEVFS_CMD_READY;
		ret = (void*)cmd;
		fd_queue->sq_head = (fd_queue->sq_head + sizeof(nvme_command_rw_t)) & (fdqueuesize - 1);
	}

	return ret;
}

static void release_vsq_entry(fd_q *fd_queue, void *ioq) {
	nvme_command_rw_t *cmd = (nvme_command_rw_t*)ioq;
	cmd->status = 0;
	return;
}

#else

void* find_avail_vsq_entry(ufile *fp) {
	int i = 0;
	fd_q *fd_queue = (fd_q*)&fp->fd_queue;
	void *ret = NULL;
	void *vsq = fd_queue->vsq;
	nvme_command_rw_t *cmd = NULL;
	nvme_command_rw_t *temp_cmd = NULL;
	nvme_command_rw_t *temp_status = NULL;
	crfs_mutex_lock(&fp->mutex);
    int head = fd_queue->sq_head;
    int queue_len = 0;
find_entry_again:
	cmd = (nvme_command_rw_t*)(fd_queue->vsq + fd_queue->sq_head);
	if (__sync_fetch_and_add(&cmd->status, 0) == 0) {
		ret = (void*)cmd;

	//} else if (__sync_fetch_and_add(&cmd->status, 0) == (DEVFS_CMD_READY | DEVFS_CMD_FINISH)) {
	} else if ((cmd->common.opc == nvme_cmd_write || 
                        cmd->common.opc == nvme_cmd_append) && 
                        (__sync_fetch_and_add(&cmd->status, 0) == (DEVFS_CMD_READY | DEVFS_CMD_FINISH | DEVFS_CMD_BUSY))) {
		if (cmd->blk_addr) {

//#ifndef CRFS_WB_CACHE
			crfs_free((void*)cmd->blk_addr);
//#else
            /* cmd->blk_addr = NULL; */
//#endif
		}
		ret = (void*)cmd;
	} else {
#ifdef MODEL_PERF
        queue_len = 1;
        while (1) {
                fp->stat.queue_len = queue_len;
            if (head == 0) head = fdqueuesize;
            head -= sizeof(nvme_command_rw_t);
            temp_cmd = (nvme_command_rw_t*)(fd_queue->vsq + fd_queue->sq_head); 
            temp_status = __sync_fetch_and_add(&temp_cmd->status, 0);
            if (temp_status == DEVFS_CMD_BUSY || temp_status == (DEVFS_CMD_BUSY|DEVFS_CMD_READY))
                queue_len++;
            else
                break;
        }
        printf("queue_len: %d\n", queue_len);
#endif
        goto find_entry_again;
    }

	memset(cmd, 0, sizeof(nvme_command_rw_t));
	cmd->status |= DEVFS_CMD_BUSY;

	fd_queue->sq_head = (fd_queue->sq_head + sizeof(nvme_command_rw_t)) & (fdqueuesize - 1);
	crfs_mutex_unlock(&fp->mutex);

	return ret;
}

static void release_vsq_entry(fd_q *fd_queue, void *ioq) {
	return;
}
#endif	//PARAFS_BYPASS_KERNEL


/*
 *  DevFS open file
 */
int vfio_crfs_open_filep(int dev, const char* fname,
		int oflags, mode_t mode, int journ, void* qaddr) {

	int ret;
	struct vfio_crfs_creatfp_cmd map;

	map.argsz = sizeof(map);
	strcpy(map.fname, fname);
	map.mode = mode; //0666;
	map.flags = oflags; //(O_CREAT | O_RDWR);
	map.isjourn = journ;
	map.iskernelio = 0;

	map.qentrycnt = qentrycount;
	map.isrdqueue = 0;
	map.vaddr = (u64)qaddr;

	ret = ioctl(dev, VFIO_DEVFS_CREATFILE_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_CREATFILE_CMD for %s  "
				"failed errno %d \n", fname, errno);
		return -errno;
	}
#if defined(_DEBUG)
	fprintf(stderr,"ioctl file open file descriptor %d \n", map.fd);
#endif
	return map.fd;
}

#ifndef PARAFS_BYPASS_KERNEL
/*
 *  DevFS close file
 */
int vfio_crfs_close_file(int dev, int fd) {

	int ret = 0;
	struct vfio_crfs_closefd_cmd map;

#if defined(_DEBUG)
	fprintf(stderr,"vfio_crfs_close_file \n");
#endif
	map.argsz = sizeof(map);
	map.fd = fd;

	ret = ioctl(dev, VFIO_DEVFS_CLOSEFILE_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_CLOSEFILE_CMD errno %d \n", errno);
		return -1;
	}
	return ret;
}
#else
int vfio_crfs_close_file(int dev, int fd) {
	int ret = 0;
#if 0 //def CRFS_OPENCLOSE_OPT
	struct vfio_crfs_closefd_cmd map;

#if defined(_DEBUG)
	fprintf(stderr,"vfio_crfs_close_file \n");
#endif
	map.argsz = sizeof(map);
	map.fd = fd;

	ret = ioctl(dev, VFIO_DEVFS_CLOSEFILE_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_CLOSEFILE_CMD errno %d \n", errno);
		return -1;
	}
	return ret;
#endif

	int opc = nvme_cmd_close;
	void *ioq = NULL;
	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		fprintf(stderr,"failed to get fs libary file pointer \n");
		return -1;
	}

	ioq = find_avail_vsq_entry(fp);
	nvme_cmd_rw_new(opc, CID, NSID, 0, 0, 0, ioq);

	ret = vfio_crfs_queue_write(dev, fd, ioq, cmd_nr);
	release_vsq_entry(&fp->fd_queue, ioq);

	return ret;
}
#endif

#ifndef PARAFS_BYPASS_KERNEL
int vfio_crfs_fsync(int fd) {
	int ret;
	struct vfio_crfs_fsync_cmd map;

	map.argsz = sizeof(map);
	map.fd = fd;

	ret = ioctl(g_dev, VFIO_DEVFS_FSYNC_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_FSYNC_CMD for %d  "
				"failed errno %d \n", fd, errno);
		return -errno;
	}

	return 0;
}
#else
int vfio_crfs_fsync(int fd) {
	int ret, i;
	int opc = nvme_cmd_flush;
	void *ioq = NULL;
	nvme_command_rw_t *cmdrw = NULL;

	ufile *fp = &ufile_table.open_files[fd];
	ufile *cur_fp = NULL;
	uinode *inode = fp->inode;

#ifdef PARAFS_FSYNC_ENABLE
	if (__sync_lock_test_and_set(&inode->fsync_barrier, 1) == 0) {
		/* Traverse FD-queue list of this inode */
		crfs_mutex_lock(&uinode_table_lock);
		inode->fsync_counter = inode->ref;
		crfs_mutex_unlock(&uinode_table_lock);

		for (i = 0; i < MAX_FP_PER_INODE; ++i) {
			cur_fp = inode->ufilp[i];

			if (cur_fp == NULL)
				continue;

			if (__sync_fetch_and_add(&cur_fp->closed, 0) == 1) {
				__sync_fetch_and_sub(&inode->fsync_counter, 1);
				continue;
			}
		}

		__sync_lock_test_and_set(&inode->fsync_barrier, 0);
		inode->fsync_counter = 0;

	} else {
		while (__sync_fetch_and_add(&inode->fsync_barrier, 0) == 1);
	}

	/* loop until fsync barrier flag is cleared */
	while (__sync_fetch_and_add(&inode->fsync_barrier, 0) == 1);
#endif

	return 0;
}
#endif

/* Check write conflct by fd-queue and then insert commands to the fd-queue. */
nvme_command_rw_t *gen_io_cmd(ufile *fp, void *buf, int opc, u64 slba,
                                u64 nlb) {
        int retval = 0;
        void *ioq = NULL;
        ufile *origin_fp = fp, *target_fp = NULL;
        nvme_command_rw_t *cmdrw = NULL;
        if(opc>max_op) return cmdrw;

        // TODO: add comments
        /* Get the submission head of FD-queue */
        ioq = find_avail_vsq_entry(fp);
        nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);
        cmdrw = (nvme_command_rw_t *)ioq;

        /* Allocate buffer for the write/append */
        if (buf == NULL) {
                goto exit_write;
        }
        cmdrw->blk_addr = (u64) buf; 

exit_write:
        return cmdrw;
}



int do_device_io(struct device_req* dev_req, ufile *fp, int opc, void* buf, off_t offset) {
    int retval = 0;
    if (dev_req != NULL) {

        for (int i = 0; i < dev_req->dev_req_num; i++) {

            /* printf("send device request, num:%d, slba: %ld, nlb: %ld, offset: %ld, buf_off: %ld\n",   */
                    /* dev_req->dev_req_num, dev_req->slba[i], dev_req->nlb[i], dev_req->slba[i] - offset, dev_req->slba[i] - offset); */

            retval += unvme_do_devfs_io_macro(g_dev, fp->fd, fp->fd_queue.vsq,
                    opc, (void *)buf + (dev_req->slba[i] - offset), dev_req->slba[i], (u64)dev_req->nlb[i], 0, NULL, NULL);
            /* printf("send device request, num:%d, slba: %ld, nlb: %ld, offset: %ld, buf_off: %ld, ret: %ld\n",   */
                    /* dev_req->dev_req_num, dev_req->slba[i], dev_req->nlb[i], dev_req->slba[i] - offset, dev_req->slba[i] - offset, retval); */
        }
    }
    return retval;
}

int do_cache_insert(uinode *inode, void* buf, off_t slba, size_t nlb, ufile *fp,
                 struct rb_root *root, int opc) {
    int retval = 0;
    struct device_req dev_req;
    int i = 0;
    int cache_rw_op = (opc == nvme_cmd_write? CACHE_WRITE_OP: CACHE_APPEND_OP);
    struct cache_req cache_req;
    dev_req.dev_req_num = 0;

#ifdef CACHE_HYBRID
    /* Add current write request to cache tree */
    if ((retval = cache_insert(fp->inode, buf, slba, nlb, fp,
                &fp->inode->cache_tree, NULL, cache_rw_op)) < 0)  {
        printf("interval tree insertion fail\n");
        retval = -1;
        goto err_insert_cache;
    }

//    retval += do_device_io(&dev_req, fp, nvme_cmd_write_cache, buf, slba);
#else

    if ((retval = cache_insert(inode, buf, slba, nlb, fp,
                root, NULL, cache_rw_op)) < 0) {
        printf("interval tree insertion fail\n");
        retval = -1;
        goto err_insert_cache;
    }

#endif

err_insert_cache:
    return retval;
}


int do_cache_read(uinode *inode, ufile *fp, off_t offset, size_t count, void* buf,
        struct rb_root *root) {

    int retval = 0;
    struct device_req dev_req;
    int i = 0;

    /* dev_req.dev_req_num = 0; */

    if ((retval = cache_read(fp->inode, fp, offset, count, (char *) buf,
                    &fp->inode->cache_tree, NULL))) {
        hit_counter++;
    }

#if 0
#ifdef CACHE_HYBRID
    retval += do_device_io(&dev_req, fp, nvme_cmd_read_cache, buf, offset);
#else
    retval += do_device_io(&dev_req, fp, nvme_cmd_read, buf, offset);
#endif
#endif

    return retval;
}

/**
 * DevFS Submit a read/write command that may require multiple I/O submissions
 * and processing some completions.
 * @param   fd          file descriptor
 * @param   ioq         io queue
 * @param   opc         op code
 * @param   buf         data buffer
 * @param   slba        starting lba
 * @param   nlb         number of logical blocks
 * @return  0 if ok else error status.
 */
int unvme_do_crfs_io(int dev, int dfd, void *ioqq, 
		int opc, void* buf, u64 slba, u64 nlb)
{
	size_t ret = -1;
	void *ioq = NULL;
	int fd = dfd;
	ufile *fp = NULL, *origin_fp = NULL, *target_fp = NULL;
	nvme_command_rw_t *cmdrw = NULL;

	fp = &ufile_table.open_files[fd];
	origin_fp = fp;

#ifdef PARAFS_BYPASS_KERNEL
	if (opc == nvme_cmd_write || opc == nvme_cmd_append) {
		/* Get the submission head of FD-queue */
#ifdef CRFS_WB_CACHE
                /* Add current write request to cache tree */
                if (do_cache_insert(fp->inode, buf, slba, nlb, fp,
                                 &fp->inode->cache_tree, opc) < 0) {
                        printf("interval tree insertion fail\n");
                        return -1;
                }
                ret = nlb;
#ifdef LEVELDB_CACHE 

                ioq = find_avail_vsq_entry(fp);
                nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);
                cmdrw = (nvme_command_rw_t*)ioq;

                /* Allocate buffer for the write/append */
                cmdrw->blk_addr = (__u64)crfs_malloc(nlb);
                memcpy((void*)cmdrw->blk_addr, (const void*)buf, nlb);
                cmdrw->status |= DEVFS_CMD_READY;
                /* printf("write to storage, slba: %ld, nlb: %ld\n", slba, nlb); */

                return nlb;

#else
                return ret;
#endif

#endif
		ioq = find_avail_vsq_entry(fp);
		nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);
		cmdrw = (nvme_command_rw_t*)ioq;

		/* Allocate buffer for the write/append */
		cmdrw->blk_addr = (__u64)crfs_malloc(nlb);
		memcpy((void*)cmdrw->blk_addr, (const void*)buf, nlb);

#ifdef CRFS_WT_CACHE
                /* Add current write request to cache tree */
                if (do_cache_insert(fp->inode, buf, slba, nlb, fp, &fp->inode->cache_tree, opc) < 0) {
                        printf("interval tree insertion fail\n");
                        return -1;

                }
#endif

		/* Mark this request as ready, then write is done :) */
		//__sync_lock_test_and_set(&cmdrw->status, DEVFS_CMD_READY);
		cmdrw->status |= DEVFS_CMD_READY;

		return nlb;
	}
#endif //PARAFS_BYPASS_KERNEL

#ifndef PARAFS_BYPASS_KERNEL
	crfs_mutex_lock(&fp->mutex);
	while (!(ioq = find_avail_vsq_entry(fp)));
	crfs_mutex_unlock(&fp->mutex);
#else
	ioq = find_avail_vsq_entry(fp);
#endif

	// Create new cmd for this request
	nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);

	// Do the actual I/O
	ret = vfio_crfs_queue_write(dev, fd, ioq, cmd_nr);

	// Release entry
	release_vsq_entry(&fp->fd_queue, ioq);

	return ret;
}

#ifdef _NVMFDQ
int global_nvm_id = 0;
crfs_mutex_t nvlock = CRFS_MUTEX_INITIALIZER;

void *align_addr(int req_size, char *fname) {

	void *bufAddr = NULL;
	unsigned long temp = 0;
	bufAddr = (char *)p_c_nvalloc_(req_size, fname, ((int)syscall(SYS_gettid))+ global_nvm_id);
	global_nvm_id++;
	temp = (unsigned long)bufAddr;
	temp = (temp & ~(PAGE_SIZE-1)) + PAGE_SIZE; 
	bufAddr = (void *)temp;
	memset(bufAddr,0,req_size);
	//fprintf(stderr, "finishing memset \n");
	return bufAddr;
}
#endif



void* vfio_queue_get_buffer(int dev, unsigned int num_pages) {

	void *bufAddr;
	int ret;
	if(num_pages == 0) return NULL;

	int req_size = (PAGE_SIZE * (num_pages+1));

#ifdef PARAFS_SHM
	bufAddr = crfs_malloc(shm, req_size);
#else
	posix_memalign(&bufAddr, PAGE_SIZE, req_size);
	mlock(bufAddr, req_size);
#endif
	if(bufAddr == NULL) {
		fprintf(stderr,"vfio_queue_get_buffer buffer create failed \n");
		return NULL;
	}

	memset(bufAddr, 0, req_size);
	struct vfio_iommu_type1_queue_map map = {
			.argsz = sizeof(map),
			.flags = (VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE),
			.vaddr = (__u64)bufAddr,
			.iova = (__u64)0, //dev->iova,
			.size = (__u64)0,
			.qpfnlist = bufAddr + (PAGE_SIZE * num_pages) + 8,
			.qpfns = (__u64)num_pages,
	};

	if (ioctl(dev, VFIO_IOMMU_GET_QUEUE_ADDR, &map) < 0) {
		fprintf(stderr,"ioctl VFIO_IOMMU_GET_QUEUE_ADDR errno %d \n", errno);
		crfs_free(bufAddr);
		return NULL;
	}

	struct vfio_crfs_creatfs_cmd fsmap = {
			.argsz = sizeof(fsmap),
			.flags = (VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE),
			.vaddr = 0,
			.iova = 0, // dev->iova,
			.nblocks = num_pages,
			.size = (__u64)(PAGE_SIZE * num_pages),
			.dev_core_cnt = (__u32)devcorecnt,
			.sched_policy = (__u32)schedpolicy,
	};

	if (ioctl(dev, VFIO_DEVFS_CREATFS_CMD, &fsmap) < 0) {
		fprintf(stderr,"VFIO_DEVFS_CREATFS_CMD errno %d \n", errno);

		exit(-1);
		return NULL;
	}

	/* Store the credential id get from OS */
	memcpy(cred_id, fsmap.cred_id, CRED_ID_BYTES);
        /* printf("create queue buffer success\n"); */
	return bufAddr;
}

static void* fd_queue_alloc(unsigned int num_pages) {
	void *bufAddr = NULL;
	crfs_mutex_lock(&fd_q_mempool.lock);
	while (fd_q_mempool.bitmap[fd_q_mempool.head] != 0)
		fd_q_mempool.head = (fd_q_mempool.head + 1) & (FD_QUEUE_POOL_PG_NUM - 1);
	fd_q_mempool.bitmap[fd_q_mempool.head] = 1;
	bufAddr = fd_q_mempool.mem + fd_q_mempool.head * PAGE_SIZE;
	fd_q_mempool.head = (fd_q_mempool.head + 1) & (FD_QUEUE_POOL_PG_NUM - 1);
	crfs_mutex_unlock(&fd_q_mempool.lock);

	return bufAddr;
}

static void fd_queue_free(void *q_addr) {
	int idx = (q_addr - fd_q_mempool.mem) >> 12;
	fd_q_mempool.bitmap[idx] = 0;
}

void* vfio_get_fd_queue_buffer(unsigned int num_pages, char *fname) {

	void *bufAddr = NULL;
	int ret;
	if (num_pages == 0)
		return NULL;

#ifdef CRFS_OPENCLOSE_OPT
	bufAddr = fd_queue_alloc(num_pages);
	if (bufAddr == NULL) {
		fprintf(stderr,"vfio_queue_get_buffer buffer create failed \n");
		return NULL;
	}

	return bufAddr;
#endif

#ifdef _NVMFDQ
	int req_size = (PAGE_SIZE * (num_pages + 5));
	crfs_mutex_lock(&nvlock);
	bufAddr = align_addr(req_size, (char *)fname);
	crfs_mutex_unlock(&nvlock);
#else
	int req_size = (PAGE_SIZE * num_pages);
        posix_memalign(&bufAddr, PAGE_SIZE, req_size);
#endif

	if (bufAddr == NULL) {
		fprintf(stderr,"vfio_queue_get_buffer buffer create failed \n");
		return NULL;
	}
#ifndef _NVMFDQ
	memset(bufAddr, 0, req_size);
#endif
	mlock(bufAddr, req_size);

	return bufAddr;
}

void vfio_put_fd_queue_buffer(void* q_addr) {
#ifndef _NVMFDQ
#ifndef CRFS_OPENCLOSE_OPT
        crfs_free(q_addr);
#else
	fd_queue_free(q_addr);
#endif

#endif
}

void set_realtime_priority() {
	int ret;

	// We'll operate on the currently running thread.
	pthread_t this_thread = pthread_self();
	struct sched_param params;
	params.sched_priority = sched_get_priority_max(SCHED_FIFO);                
	ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
	if (ret != 0) {
		return; 
	}
}

int set_affinity() {
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread = pthread_self();
	assert(thread);
	/* Set affinity mask*/
	CPU_ZERO(&cpuset);
	for (j = 16; j < 30; j++) 
		CPU_SET(j, &cpuset);
	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) { 
		fprintf(stderr, "failed pthread_setaffinity_np");
		return -1;
	}    
	return 0;
}

void* run_evict_thread(void* arg) {

    struct timeval prev_t, now_t;
    double sec = 0;
    /* signal(SIGALRM, evict_handler); */
    /* alarm(1); */

    while (1) {
        /* usleep(5000); */
        sleep(1);
        if (stopeviction) break;
        start_evict();

    }

}

void* run_dev_thread(void* arg) {
	struct vfio_crfs_init_devthread_cmd map = {
			.argsz = sizeof(map),
			.dev_core_cnt = (__u32)devcorecnt,
			.thread_index = (__u32)*(int*)arg,
			.sched_policy = (__u32)schedpolicy,
        };

	//set_realtime_priority();
//#ifdef CACHE_HYBRID
//	set_affinity();
//#endif

#ifdef REDUCE_CPU_FREQ
	set_affinity();
#endif
	if (ioctl(g_dev, VFIO_DEVFS_INIT_DEVTHREAD_CMD, &map) < 0) {
		fprintf(stderr,"VFIO_DEVFS_INIT_DEVTHREAD_CMD errno %d \n", errno);
	}
	return NULL;
}

int initialize_crfs(unsigned int qentry_count,
		     unsigned int dev_core_cnt,
		     unsigned int sched_policy, unsigned int cxl_ns_size) {
	u64 datasize = 4096;
	u64* p = NULL;
	int q = 0, fd = -1;
	int perm = CREATDIR, i = 0;
    char *shell_host_cache_env = NULL;
    char *shell_dev_cache_env = NULL;
    void* cxl_mem_addr = NULL;

//#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        lru_list_head.next = &lru_list_tail;
        lru_list_tail.prev = &lru_list_head;

        closed_file_head.next = &closed_file_tail;
        closed_file_tail.prev = &closed_file_head;
/* #ifdef CACHE_EVICT */
        shell_host_cache_env = getenv("HOST_CACHE_LIMIT_ENV");
        shell_dev_cache_env = getenv("DEV_CACHE_LIMIT_ENV");
/* #endif */
        if (shell_host_cache_env) {
            HOST_CACHE_LIMIT = strtoul(shell_host_cache_env, NULL, 10);
            /* printf("host cache limit: %ld\n", HOST_CACHE_LIMIT); */
        } else {
            /* printf("host cache is unlimited.\n"); */
        }

        if (shell_dev_cache_env) {
            DEV_CACHE_LIMIT = strtoul(shell_dev_cache_env, NULL, 10);
            /* printf("dev cache limit: %ld\n", DEV_CACHE_LIMIT); */
        } else {
            /* printf("dev cache is unlimited.\n"); */
        }
        CACHE_LIMIT = HOST_CACHE_LIMIT + DEV_CACHE_LIMIT;
        /* printf("total cache limit: %ld\n", CACHE_LIMIT); */
//#endif

#ifdef COMPRESS_CACHE
        if (!g_snappy_init) {
            if (snappy_init_env(&g_snappy_env)) {
                printf("failed to init snappy environment\n");
                return -1;

            }
            g_snappy_init = 1;
        }
#endif

#ifdef PARAFS_SCHED_THREAD
	pthread_t tid;
#endif

#ifdef _NVMFDQ
	nvinit(getpid());
#endif

#ifdef PARAFS_SHM
        crfs_mm_init();
#endif

	// Set qentrycount, devcorecnt and schedpolicy
	if (qentry_count)
		qentrycount = qentry_count;
	if (dev_core_cnt)
		devcorecnt = dev_core_cnt;
	if (sched_policy)
		schedpolicy = sched_policy;

	g_dev = open(TEST, CREATDIR, MODE);

	if(g_dev == -1){
		printf("Error!");
		exit(1);
	}

	//vsq = vfio_queue_get_buffer(g_dev, 1);
	vsq = vfio_queue_get_buffer(g_dev, FD_QUEUE_POOL_PG_NUM);
	//g_vsq = vsq;
	fd_q_mempool.mem = vsq;
	memset(fd_q_mempool.bitmap, 0, FD_QUEUE_POOL_PG_NUM*sizeof(int));
	fd_q_mempool.head = 0;
	crfs_mutex_init(&fd_q_mempool.lock);

#ifdef CXL_MEM
    cxl_mem_addr = cxl_mem_init(g_dev, CXL_MEM_PG_NUM, cxl_ns_size);
#endif
    if (cxl_mem_addr != NULL)
        printf("cxl memory init successful\n");

    /* Initialize user level inode table */
	//memset(ufile_table.open_inodes, 0, MAX_OPEN_INODE*sizeof(uinode));
	memset(ufile_table.open_files, 0, MAX_OPEN_INODE*sizeof(ufile));
	//crfs_mutex_init(&ufile_table.inode_table_lock, NULL);

#ifdef PARAFS_SCHED_THREAD
	/*
	 * If using pthread as device thread, just call
	 * run_dev_thread() to initialize
	 */
	for (i = 0; i < devcorecnt; ++i) {
		pthread_create(&tid, NULL, &run_dev_thread, &i);
		//pthread_detach(tid);
		sleep(1);
	}
#endif

//#ifdef _USE_OPT
	//setHandler(fault_handler);	
//#endif

#ifdef CACHE_EVICT
        if (CACHE_LIMIT!=0) {
                pthread_attr_t tattr;
                int newprio = 99;
                struct sched_param param;
                pthread_attr_init (&tattr);
                pthread_attr_getschedparam (&tattr, &param);
                param.sched_priority = newprio;

                pthread_attr_setschedparam (&tattr, &param);


                /* cache_thpool = thpool_init(1); */
                //pthread_create(&tid, &tattr, &run_evict_thread, NULL);
        }
#endif
	return 0;
}


int shutdown_crfs()
{
	int ret;
	struct vfio_crfs_closefs_cmd map;

	map.argsz = sizeof(map);
	memcpy(map.cred_id, cred_id, CRED_ID_BYTES);
        /* printf("cache hit: %ld\n", hit_counter); */

#if 1
        stopeviction = 1;
#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        /* printf("start dropcache\n"); */
#ifndef LEVELDB_CACHE
        drop_all_cache(all_inodes, all_files_cnt);
#endif
#endif
#endif

	ret = ioctl(g_dev, VFIO_DEVFS_CLOSEFS_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_CLOSEFS_CMD for %s  "
				"failed errno %d \n", fname, errno);
		return -errno;
	}
#ifdef CXL_MEM
    if(cxl_mempool.mem) {

        printf("free cxl memory\n");
        free(cxl_mempool.mem);
    }
#endif

	/*if (fd_q_mempool.mem)
		free(fd_q_mempool.mem);*/

	isexit = 1;

#ifdef PARAFS_STAT
	crfs_stat_fp_queue_count(); 
#endif

	return 0;
}


/****************************************************************
 * Entry function for each POSIX I/O syscalls for DevFS
 * *************************************************************/

/*
 * Open file from crfs
 */
int crfs_open_file(const char *fname, int perm, mode_t mode)
{
	int fd = -1;
	
	ufile *fp = NULL;
	uinode *inode = NULL;

	g_slba = 0;

	if(!qentrycount)
		qentrycount = 1;

	// Allocate 1 page for command buffer
	void *qaddr = vfio_get_fd_queue_buffer(FD_QUEUE_PAGE_NUM, (char *)fname);

	fd = vfio_crfs_open_filep(g_dev, (char *)fname, perm, mode, isjourn, qaddr);
	if(fd < 0) {
		fprintf(stderr,"crfs_open_file failed %d \n", errno);
		return fd;
	}

	/* Setup user-level file pointer */
	fp = &ufile_table.open_files[fd];

	/* Wait until this user-level file pointer is released by another thread */
	while (fp->fd > 0);

	fp->fd = fd;
	fp->ref = 1;
	fp->off = 0;
	fp->flush_off = 0;
	fp->fd_queue.vsq = qaddr;
	fp->fd_queue.sq_head = 0;
	fp->fd_queue.size = fdqueuesize;
        strcpy(fp->fname, fname);
        
	/* Setup user-level inode */
	inode = get_inode(fname, fp);	
	if (inode == NULL) {
		fprintf(stderr,"uinode not found\n");
		exit(-1);
	}

	// If fsync barrier is set, should spin here
#ifdef PARAFS_FSYNC_ENABLE
	/* loop until current fsync is done */
	while (__sync_fetch_and_add(&inode->fsync_barrier, 0) == 1);
#endif

	/* Setup FD-queue */
	crfs_mutex_init(&fp->mutex);
	pthread_cond_init(&fp->cond, NULL);

	fp->closed = 0;
	fp->closed_conflict = 0;

	return fd;
}


/*
 * Open file from crfs
 */
int crfs_close_file(int fd)
{
	ufile *fp = NULL;
	uinode *inode = NULL;

	int numclose = 0;

	if(fd < 0) {
		fprintf(stderr,"crfs_open_file failed %d \n", errno);
	}

	fp = &ufile_table.open_files[fd];
	inode = fp->inode;
    
#ifdef PARAFS_FSYNC_ENABLE
	/* loop until current fsync is done */
	while (__sync_fetch_and_add(&inode->fsync_barrier, 0) == 1);

	/* mark this fp is being closed, do not insert fsync to this FD-queue */
	__sync_lock_test_and_set(&fp->closed, 1);
#endif

#if defined CRFS_WB_CACHE && !defined LEVELDB_CACHE

        __sync_lock_test_and_set(&fp->closed, 0);
        crfs_mutex_lock(&uinode_table_lock);
        inode->open_ref--;
        if (inode->open_ref == 0) {
                /* printf("add inode close, fname: %s, open_ref: %d\n", fp->fname, inode->open_ref); */
                add_to_lru_list(&inode->close_node, &closed_file_tail, &lru_closed_files_list_lock);
                /* list_del(&inode->open_node); */
                //printf("add to tail\n");
        }
        crfs_mutex_unlock(&uinode_table_lock);
#else

#ifdef LEVELDB_CACHE
        if(inode->ref == 1) {
            /* printf("flush fp, fname: %s\n", fp->fname); */
            flush_fp(fp->fd, fp, fp->inode, &fp->inode->cache_tree);
        }
#endif

        if (vfio_crfs_close_file(g_dev, fd) < 0) {
                fprintf(stderr, "crfs_close_file failed %d \n", errno);
        }

        __sync_lock_test_and_set(&fp->closed, 0);
        /* Remove user-level file pointer in user-level inode */
        put_inode(inode, fp);

        /* Free fd-queue buffer */
        vfio_put_fd_queue_buffer(fp->fd_queue.vsq);

        fp->fd = -1;
        fp->ref = 0;
        fp->off = -1;
#endif

	return 0;
}

/*Append write to existing file position*/
size_t crfs_write(int fd, const void *p, size_t count) {

	u64 nlb = 0;
	size_t write_cnt = 0, left = count;
	int written = 0;
	int retval;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	nlb = fp->off;
	retval = unvme_do_crfs_io(g_dev, fd, fp->fd_queue.vsq, nvme_cmd_append, (void *)p, nlb, (u64)count);
	if (retval < 0){
		printf("crfs_write failed %d\n", retval);
		return -1;
	}

	fp->off += retval;

        //printf("crfs write, fd: %d, count: %lu, offset: %ld, fname: %s, ret: %d\n", fd, count, fp->off, fp->fname, retval);
	return retval;
}

size_t crfs_pwrite(int fd, const void *p, size_t count, off_t offset) {

	u64 nlb = 0;
	size_t write_cnt = 0, left = count;
	int written = 0;
	int retval;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	nlb = offset;

    /* printf("crfs pwrite, fd: %d, offset: %lu, count: %lu, fname: %s\n", fd, offset, count, fp->fname); */

	retval = unvme_do_crfs_io(g_dev, fd, fp->fd_queue.vsq, nvme_cmd_write, (void *)p, nlb, (u64)count);
	if (retval < 0){
		printf("crfs_write failed %d\n", retval);
		return -1;
	}

    /* printf("crfs pwrite, fd: %d, offset: %lu, count: %lu, fname: %s, ret: %d\n", fd, offset, count, fp->fname, retval); */

	return (size_t)retval;
}


/*Append write to existing file position*/
size_t crfs_read(int fd, void *p, size_t count) {

	u64 nlb = 0;
	int retval = 0;
        int cache_ret = 0;
        int io_ret = 0;


	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	// mark as -1 to indicate this is a read call
	nlb = INVALID_SLBA;

    /* printf("read, fd: %d, count = %d, offset = %d, fname: %s\n", fd, count, fp->off, fp->fname); */
#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        if(cache_ret = do_cache_read(fp->inode, fp, fp->off, count, p, &fp->inode->cache_tree)) {
                retval += cache_ret;
        }

        if (cache_ret>= count) {
                fp->off += cache_ret;
                hit_counter++;
                debug_printf("read, fd: %d, count = %d, offset = %d, ret: %d\n", fd, count, fp->off, retval);
                return retval;
        }
#endif

	io_ret = unvme_do_crfs_io(g_dev, fd, fp->fd_queue.vsq, nvme_cmd_read, p, nlb, (u64)count);
	if (io_ret < 0){
		printf("crfs_read failed fd = %d, pos = %lu, count = %lu \n", fd, nlb, count);
		return -1;
	}

	retval += io_ret;
#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        debug_printf("read %d bytes, insert it back to cache, start: %ld, end: %ld\n", io_ret, fp->off, fp->off + io_ret - 1);
        if (io_ret > cache_ret && do_cache_insert(fp->inode, p, fp->off, io_ret , fp,
                         &fp->inode->cache_tree, nvme_cmd_write) < 0) {
                printf("interval tree insertion fail\n");
                return -1;
        }
#endif

	retval = io_ret > cache_ret?io_ret:cache_ret;
	fp->off += retval;

    /* printf("read, fd: %d, count = %d, offset = %d, ret: %d\n", fd, count, fp->off, retval); */

	return (size_t)retval;
}

size_t crfs_pread(int fd, void *p, size_t count, off_t offset) {

        int retval = 0;
        int io_retval = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        /* printf("pread, fd: %d, count = %d, offset = %d, fname: %s\n", fd, count, offset, fp->fname); */
#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        if ((retval = do_cache_read(fp->inode, fp, offset, count, p,
                    &fp->inode->cache_tree))) {
                hit_counter++;
        }
        if (retval >= count) {
                /* printf("pread, fd: %d, count: %ld, offset: %ld, fname: %s, ret: %d done\n",fd, count, offset, fp->fname, retval); */
                return count;
        }
#endif

        offset += retval;
        count -= retval;


        /* printf("fd: %d, ioread, offset: %ld, count: %ld\n", fd, offset, count); */
        io_retval = unvme_do_crfs_io(g_dev, fd, fp->fd_queue.vsq, nvme_cmd_read, p, offset, (u64)count);
        if (io_retval < 0) {
                printf("crfs_pread failed fd = %d, pos = %lu, count = %lu \n", fd, offset, count);
                return -1;
        }

#if defined CRFS_WT_CACHE || defined CRFS_WB_CACHE
        /* printf("fd: %d, cache_ret: %ld, io_ret: %ld, insert it back to cache, before start: %ld , start: %ld, end: %ld\n",  */
                /* fd, retval, io_retval, offset - retval, offset, offset + io_retval - 1); */
        if (io_retval > 0 && do_cache_insert(fp->inode, p, offset, io_retval, fp,
                         &fp->inode->cache_tree, nvme_cmd_write) < 0) {
                printf("interval tree insertion fail\n");
                return -1;
        }
#endif
        retval += io_retval;

        /* printf("pread, fd: %d, count: %ld, offset: %ld, fname: %s, ret: %d done\n",fd, count, offset, fp->fname, retval); */

        return (size_t)retval;
}


int crfs_lseek64(int fd, off_t offset, int whence) {

	u64 slba = whence;
	int retval;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	if (whence == SEEK_SET) {
		fp->off = offset;
	} else if (whence == SEEK_CUR) {
		fp->off += offset;
	} else {
		//TODO
	}

	retval = fp->off;

	/* 
	 * Since we offload file offset managing in user space
	 * we don't have to send io command any more
	 */

	/*retval = unvme_do_crfs_io(g_dev, fd, g_vsq, nvme_cmd_lseek, NULL, slba, (u64)offset);
	return (size_t)offset;
	if (retval < 0){
		printf("crfs_lseek64 failed %d\n", retval);
		return -1;
	}*/

#if defined(_DEBUG)
	printf("crfs_write read at %zu retval \n", retval);
#endif
	return (size_t)retval;
}

int crfs_fsync(int fd)
{
	int ret;
	struct vfio_crfs_fsync_cmd map;

	map.argsz = sizeof(map);
	map.fd = fd;

	ret = vfio_crfs_fsync(fd);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_FSYNC_CMD for %d  "
				"failed errno %d \n", fd, errno);
		return -errno;
	}

	return 0;
}

int crfs_fallocate(int fd, off_t offset, off_t len)
{
        return 0;
}

int crfs_ftruncate(int fd, off_t length)
{
        return 0;
}

int crfs_unlink(const char *pathname)
{
	int ret;
	struct vfio_crfs_unlink_cmd map;

	map.argsz = sizeof(map);
	map.uptr = (u64)pathname;

	ret = ioctl(g_dev, VFIO_DEVFS_UNLINK_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_UNLINK_CMD for %s  "
				"failed errno %d \n", pathname, errno);
		return -errno;
	}

	return 0;

}

int crfs_rename(const char *oldpath, const char *newpath)
{
	int ret;
	struct vfio_crfs_rename_cmd map;

    printf("rename, old: %s, new: %s\n", oldpath, newpath);
#if defined CRFS_WT_CACHE && !defined LEVELDB_CACHE
	    crfs_mutex_lock(&uinode_table_lock);
        printf("rename, old: %s, new: %s\n", oldpath, newpath);
        uinode *target = NULL;
        target = uinode_table_lookup(oldpath);
        if (target) {
                uinode_table_delete(target);
                strcpy(target->fname, newpath);
                uinode_table_insert(target);
        }
        printf("rename, old: %s, new: %s done\n", oldpath, newpath);
	    crfs_mutex_unlock(&uinode_table_lock);
#endif
	map.argsz = sizeof(map);
	map.oldname = (u64)oldpath;
	map.newname = (u64)newpath;

	ret = ioctl(g_dev, VFIO_DEVFS_RENAME_CMD, &map);
	if (ret < 0) {
		fprintf(stderr,"ioctl VFIO_DEVFS_UNLINK_CMD for %s %s "
				"failed errno %d \n", oldpath, newpath, errno);
		return -errno;
	}

	return 0;

}



u32 get_compound_vir_time(int opc, nvme_command_rw_t *cmd) {
    if (opc != nvme_cmd_compound) {
        printf("failed, not a compound operations\n");
        return 0;
    }
    int i = 0;
    u32 vir_runtime = 0;
    
    for (i = 0; i < cmd->common.num_op; i++) {
        vir_runtime += get_vir_time(cmd->common.opc_vec[i]); 
    }
    return vir_runtime;
}

/* Pack compand I/O command vectors */
int nvme_cmd_macro_iov(int opc, void *vsq, struct macro_op_desc *desc)
{
	nvme_command_rw_t *cmd = (nvme_command_rw_t*)vsq;
	/* Setup I/O vec for compound op */
	cmd->common.num_op = desc->num_op;
	for (int i = 0; i < desc->num_op; ++i) {
		cmd->common.prp_vec[i] = desc->iov[i].prp;
		cmd->common.opc_vec[i] = desc->iov[i].opc;	
		if (desc->iov[i].opc == nvme_cmd_match) {
			cmd->param_vec[i].cond_param.addr = desc->iov[i].addr;
			cmd->param_vec[i].cond_param.nlb = desc->iov[i].nlb;
		} else {
			cmd->param_vec[i].data_param.slba = desc->iov[i].slba;
			cmd->param_vec[i].data_param.nlb = desc->iov[i].nlb;
		}
	}
	if (opc == nvme_cmd_compound && desc->num_op > 0) {
		cmd->vir_runtime = get_compound_vir_time(opc, cmd);
	}
#if defined(_DEBUG)
	printf("cmd->slba %llu cmd->nlb %llu, compoud cmd virtual time: %lu\n", cmd->slba, cmd->nlb, cmd->vir_runtime);
#endif
	return 0;
}

/**
 * DevFS Submit a read/write command that may require multiple I/O submissions
 * and processing some completions.
 * @param   fd          file descriptor
 * @param   ioq         io queue
 * @param   opc         op code
 * @param   buf         data buffer
 * @param   slba        starting lba
 * @param   nlb         number of logical blocks
 * @return  0 if ok else error status.
 */
int unvme_do_devfs_io_macro(int dev, int dfd, void *ioqq, 
	int opc, void* buf, u64 slba, u64 nlb, uint8_t crc_pos,
	struct macro_op_desc *desc, void* blk_addr)
{
	size_t ret = -1;
	void *ioq = NULL;
	int fd = dfd;
	nvme_command_rw_t *cmdrw = NULL;
	ufile *fp = NULL;
	fp = &ufile_table.open_files[fd];

#ifdef PARAFS_BYPASS_KERNEL
	if (opc == nvme_cmd_write || opc == nvme_cmd_append) {
		/* Get the submission head of FD-queue */
                printf("unvme_do_devfs_io_macro, op: %d, async\n", opc);
		ioq = find_avail_vsq_entry(fp);

		nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);
                nvme_cmd_macro_iov(opc, ioq, desc);

		cmdrw = (nvme_command_rw_t*)ioq;

		/* Add crc_pos to the new command */
		cmdrw->meta_pos = crc_pos;

                /* Allocate buffer for the write/append */
                cmdrw->blk_addr = (__u64)crfs_malloc(nlb);
                memcpy((void*)cmdrw->blk_addr, (const void*)buf, nlb);

		/* Mark this request as ready, then write is done :) */
		cmdrw->status |= DEVFS_CMD_READY;
		//__sync_lock_test_and_set(&cmdrw->status, DEVFS_CMD_READY);              

		return nlb;
	}
#endif //PARAFS_BYPASS_KERNEL

	ioq = find_avail_vsq_entry(fp);

	// Create new cmd for this request
    nvme_cmd_rw_new(opc, CID, NSID, slba, nlb, (u64)buf, ioq);

    if (desc != NULL)
        nvme_cmd_macro_iov(opc, ioq, desc);

	cmdrw = (nvme_command_rw_t*)ioq;
	/* Add crc_pos to the new command */
	cmdrw->meta_pos = crc_pos;
    if (blk_addr!=NULL)
        cmdrw->blk_addr = blk_addr;

	// Do the actual I/O
	ret = vfio_crfs_queue_write(dev, fd, ioq, g_vsqlen);

#ifdef MODEL_PERF
    fp->stat.op_device_count++;
    fp->stat.total_device_exec_time += cmdrw->common.exec_time;
    fp->stat.bw_dm_to_hm = cmdrw->common.bw_dm_h;
    fp->stat.bw_ds_to_hm = cmdrw->common.bw_ds_dm;

    if (fp->stat.op_device_count >= stat_interval) {
        fp->stat.avg_device_exec_time = fp->stat.total_device_exec_time / fp->stat.op_device_count;
        fp->stat.op_device_count = 0;
        fp->stat.total_device_exec_time = 0;
    }
#endif
    // Release entry
	release_vsq_entry(&fp->fd_queue, ioq);

	return ret;
}

/* Compound POSIX calls (containing computing along with I/O command) */
size_t devfs_readmodifywrite(int fd, const void *p, size_t count, off_t offset) {

	u64 slba = 0; 
	size_t write_cnt = 0, left = count;
	int written = 0; 
	int retval;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd); 
		return -1;
	}    

	slba = offset;

        //printf("readmofifywrite, fd: %d, count: %d, offset: %d \n", fd, count, offset);

	/* Build I/O vec */
	desc = readmodifywrite_cmd(p, count, offset);

	retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_read_modify_write, (void *)p, slba, (u64)count, 0, &desc, NULL);

        //printf("readmofifywrite, fd: %d, count: %d, offset: %d, ret: %d \n", fd, count, offset, retval);

	if (retval < 0){
	        printf("devfs_readmodifywrite failed fd = %d, %d\n", fd, retval);
		return -1;
	}    

	return (size_t)retval;
}

size_t devfs_readmodifywrite_batch(int fd, const void **p, size_t count, off_t* offsets, size_t batch_size){

	u64 slba = 0; 
	size_t write_cnt = 0, left = count;
	int written = 0; 
	int retval;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd); 
		return -1;
	}    

	slba = fp->off;

        /* printf("readmofifywrite, fd: %d, count: %d, offset: %d \n", fd, count, offsets[0]); */

	/* Build I/O vec */
        desc.num_op = batch_size;
        for (int i = 0; i < batch_size; i++) {
            desc.iov[i].opc = nvme_cmd_append;
            desc.iov[i].prp = (uint64_t)p[i];
            desc.iov[i].slba = offsets[i];
            desc.iov[i].nlb = count;
        }

	retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_read_modify_write_batch, (void *)p[0], slba, (u64)count, 0, &desc, NULL);

        /* printf("readmofifywrite, fd: %d, count: %d, offset: %d, ret: %d \n", fd, count, offsets[0], retval); */

	if (retval < 0){
	        printf("devfs_readmodifywrite failed fd = %d, %d\n", fd, retval);
		return -1;
	}    
        fp->off += retval;

	return (size_t)retval;
}

size_t devfs_readmodifyappend(int fd, const void *p, size_t count) {

	u64 slba = 0; 
	size_t write_cnt = 0, left = count;
	int written = 0; 
	int retval;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd); 
		return -1;
	}    

	slba = fp->off;

	/* printf("readmofifywrite, fd: %d, count: %d, offset: %d \n", fd, count, offset); */

	/* Build I/O vec */
	desc = readmodifyappend_cmd(p, count);

	retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_read_modify_append, (void *)p, slba, (u64)count, 0, &desc, NULL);

	/* printf("readmofifywrite, fd: %d, count: %d, offset: %d, ret: %d \n", fd, count, offset, retval); */

	if (retval < 0){
	        printf("devfs_readmodifywrite failed fd = %d, %d\n", fd, retval);
		return -1;
	}    
        fp->off += retval;

	return (size_t)retval;
}

/*Append write to existing file position*/
size_t devfs_checksumwrite(int fd, const void *p, size_t count, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = fp->off;

        /* Build I/O vec */
        desc = checksumwrite_cmd(p, count);

#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_append_chksm, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)count, crc_pos, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_checksumwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return retval;
}

size_t devfs_checksumpwrite_batch(int fd, const void **p, size_t count, 
                off_t* offsets, uint8_t crc_pos, size_t batch_size) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offsets[0];

        /* Build I/O vec */

        desc.num_op = batch_size;
        for (int i = 0; i < batch_size; i++) {
                desc.iov[i].opc = nvme_cmd_write;
                desc.iov[i].prp = (uint64_t)p[i];
                desc.iov[i].slba = offsets[i];
                desc.iov[i].nlb = count;
        }

        /* printf("devfs_write writing at %zu offset \n", count); */

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                       nvme_cmd_write_chksm_batch, (void *)p[0], slba, (u64)count, crc_pos, &desc, NULL);
        
        if (retval < 0){
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        /* printf("devfs_write wrote at %zu retval \n", retval); */
        return (size_t)retval;
}

size_t devfs_appendchksmpwrite_cache_hybrid(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {
        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
	    uint32_t crc = 0;
        int cache_in_kernel = 0;
        struct device_req dev_req;
        int i = 0;

        dev_req.dev_req_num = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);

        /* Put checksum into write block */
        memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

        if ((retval = cache_insert(fp->inode, (char *)p, slba, count, fp,
                    &fp->inode->cache_tree, NULL, CACHE_APPEND_OP)) < 0) {
            printf("interval tree insertion fail\n");
            return -1;
        }
            
        /* host cache is full, data need to cached in device */
        for (i = 0; i < dev_req.dev_req_num; i++) {

            debug_printf("host cache is full, write device cache,num:%d, slba: %ld, nlb: %ld, offset: %ld\n", 
                            dev_req.dev_req_num, dev_req.slba[i], dev_req.nlb[i], dev_req.slba[i] - slba);

            retval += unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                    nvme_cmd_write_cache, (void *)p + (dev_req.slba[i] - slba), dev_req.slba[i], (u64)dev_req.nlb[i], crc_pos, NULL, NULL);

        }


        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        retval = count;

        return (size_t)retval;
}

size_t devfs_appendchksmpwrite_cache_kernel(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        uint32_t crc = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_append_chksm_write_cache, (void *)p, slba, (u64)count, crc_pos, NULL, NULL);

        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        return (size_t)retval;
}

size_t devfs_readchksmpwrite_cache_kernel(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
	    uint32_t crc = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                nvme_cmd_read_chksm_write_cache, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);

        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        return (size_t)retval;
}


size_t devfs_read_knn_write_hybrid(int fd, const void *p, size_t count, off_t offset) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
	    uint32_t crc = 0;
        unsigned long data_idx = 0;
        unsigned long prev_data_idx = 0;

        struct cache_req cache_req;
        memset(&desc, 0, sizeof(struct macro_op_desc));
        memset(&cache_req, 0, sizeof(struct cache_req));

        char buf[READ_BUF_SIZE];
        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }
        PredictingCase* predicting_cases = (PredictingCase*) malloc(NUM_PREDICTING_CASES * sizeof(PredictingCase));;
        unsigned int* distance_arr = malloc(NUM_PREDICTING_CASES * NUM_TRAIN_CASES *sizeof(unsigned int));

        gen_predicting_data(predicting_cases);

        while(1) {

            retval = cache_read(fp->inode, fp, slba, READ_BUF_SIZE, (char *) buf,
                                        &fp->inode->cache_tree, &cache_req);

            if (retval <= 0 || cache_req.node_num <= 0) {
                break;
            }

            if (cache_req.in_host_num > cache_req.in_device_num + cache_req.in_storage_num) {

                load_data_to_host(fp, &cache_req);

                prev_data_idx = data_idx;
                data_idx = read_knn_data_buf(fd, buf, data_idx);

                calc_distance(distance_arr, predicting_cases, prev_data_idx, data_idx - prev_data_idx);
            } else {
                offload_data_to_device(fp, &cache_req, &desc);

                retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                    nvme_cmd_read_knn_write, (void *)predicting_cases, data_idx, (u64)NUM_PREDICTING_CASES * sizeof(PredictingCase), 0, &desc,distance_arr);
            }
        }

        prediction(distance_arr);

        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        free(distance_arr);
        free(predicting_cases);
}

size_t devfs_read_knn_write(int fd, const void *p, size_t count, off_t offset) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
	    uint32_t crc = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        unsigned long train_cases_width = NUM_TRAIN_CASES / READ_BUF_SIZE;

        PredictingCase* predicting_cases = (PredictingCase*) malloc(NUM_PREDICTING_CASES * sizeof(PredictingCase));
        unsigned int* distance_arr = malloc(NUM_PREDICTING_CASES * NUM_TRAIN_CASES *sizeof(unsigned int));
        gen_predicting_data(predicting_cases);

        for (unsigned long i = 0; i < NUM_TRAIN_CASES; i += train_cases_width) {

            read_knn_data(fd, i, train_cases_width);

            calc_distance(distance_arr, predicting_cases, i, train_cases_width);
        }

        prediction(distance_arr);

        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        free(distance_arr);
        free(predicting_cases);
        return (size_t)retval;

#if 0
        PredictingCase* predicting_cases = (PredictingCase*) malloc(NUM_PREDICTING_CASES * sizeof(PredictingCase));;
        gen_predicting_data(predicting_cases);

        printf("nvme_cmd_read_knn_write\n");
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                nvme_cmd_read_knn_write, (void *)predicting_cases, slba, (u64)NUM_PREDICTING_CASES * sizeof(PredictingCase), 0, NULL);

        unsigned int* distance_arr = malloc(NUM_PREDICTING_CASES * NUM_TRAIN_CASES *sizeof(unsigned int));

        read_knn_data(fd);

        gen_predicting_data(predicting_cases);

        calc_distance(distance_arr, predicting_cases);

        prediction(distance_arr);

        if (retval < 0) {
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        free(distance_arr);
        free(predicting_cases);
#endif
}

#if 0
size_t devfs_readchksmpwrite_cache_hybrid_model(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

    u64 slba = 0;
    size_t write_cnt = 0, left = count;
    int written = 0;
    int retval;
    struct macro_op_desc desc;
    uint32_t crc = 0;
    struct cache_req cache_req;
    int i = 0;
    struct timeval st, et;
    double exc_time = 0;
    double model_h = 0;
    double model_d = 0;

    printf("devfs_readchksmpwrite_cache_hybrid_model\n");

    ufile *fp = &ufile_table.open_files[fd];
    if (!fp) {
        printf("failed to get ufile %d\n", fd);
        return -1;
    }

    slba = offset;

    if ((retval = cache_read(fp->inode, fp, offset, count, (char *) p,
                    &fp->inode->cache_tree, &cache_req))) {
        hit_counter++;
    }

    if (fp->stat.bm_to_hm == 0) {
        fp->stat.bm_to_hm = 1;
    }

    printf("cache read done, retval: %ld, offset: %ld, in_host_num: %ld\n", retval, offset, cache_req.in_host_num);

    model_h = cache_req.in_device_num * NODE_SIZE_LIMIT / BLOCKSIZE * fp->stat.bm_to_hm;
    model_h += fp->stat.avg_host_exec_time;

    model_d = cache_req.in_host_num * NODE_SIZE_LIMIT / BLOCKSIZE * fp->stat.bm_to_hm;
    model_d += fp->stat.avg_device_exec_time;

    if (model_h > model_d) {

        gettimeofday(&st, NULL);
        crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);
        gettimeofday(&et, NULL);

        exc_time = simulation_time_us(st, et);

        fp->stat.op_host_count++;
        fp->stat.total_host_exec_time += exc_time;

        if (fp->stat.op_host_count >= stat_interval) {
            fp->stat.avg_host_exec_time = fp->stat.total_host_exec_time / fp->stat.op_host_count;
            fp->stat.op_host_count = 0;
        }

        /* Put checksum into write block */
        memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

        if (cache_insert(fp->inode, (char *)p, fp->off, count, fp,
                    &fp->inode->cache_tree, &cache_req, CACHE_WRITE_OP) < 0) {
            printf("interval tree insertion fail\n");
            return -1;
        }
        
    } else {

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                nvme_cmd_read_chksm_write_cache, (void *)p, slba, (u64)count, crc_pos, &desc);
        fp->stat.op_device_count++;
        fp->stat.total_device_exec_time += exc_time;

        if (fp->stat.op_device_count >= stat_interval) {
            fp->stat.avg_device_exec_time = fp->stat.total_device_exec_time / fp->stat.op_device_count;
            fp->stat.op_device_count = 0;
        }
    }

    retval = count;
    return (size_t)retval;
}
#else

size_t devfs_readchksmpwrite_cache_hybrid_model(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

    u64 slba = 0;
    size_t write_cnt = 0, left = count;
    int written = 0;
    int retval;
    struct macro_op_desc desc;
    uint32_t crc = 0;
    struct cache_req cache_req;
    int i = 0;
    struct timeval st, et;
    double exc_time = 0;
    double model_h = 0;
    double model_d = 0;

    memset(&desc, 0, sizeof(struct macro_op_desc));
    memset(&cache_req, 0, sizeof(struct cache_req));

    ufile *fp = &ufile_table.open_files[fd];
    if (!fp) {
        printf("failed to get ufile %d\n", fd);
        return -1;
    }

    slba = offset;

    cache_read(fp->inode, fp, offset, count, (char *) p,
            &fp->inode->cache_tree, &cache_req);

    if (cache_req.node_num <= 0 ) {
        return -1;
    }

    if (fp->stat.bw_dm_to_hm <= 0) {
        fp->stat.bw_dm_to_hm = 1;
    }
    
    if (fp->stat.bw_ds_to_hm <= 0) {
        fp->stat.bw_ds_to_hm = 1;
    }

    /* printf("inhost_num: %ld, in_device_num: %d, in_storage_num: %d\n", cache_req.in_host_num, cache_req.in_device_num, cache_req.in_storage_num); */

    model_h = cache_req.in_device_num  / fp->stat.bw_dm_to_hm;
    model_h += cache_req.in_storage_num / fp->stat.bw_ds_to_hm;
    model_h += fp->stat.avg_host_exec_time;

    model_d = cache_req.in_host_num / fp->stat.bw_dm_to_hm;
    model_d += cache_req.in_storage_num / fp->stat.bw_ds_to_hm;
    model_d += fp->stat.avg_device_exec_time;
    model_d += fp->stat.queue_len *  fp->stat.avg_device_exec_time;

   // printf("model_h: %lf, model_d: %lf, avg_host: %lf, avg_device: %lf, dm_hm: %lf, ds_to_hm: %lf\n", model_h, model_d, fp->stat.avg_host_exec_time, fp->stat.avg_device_exec_time, fp->stat.bw_dm_to_hm, fp->stat.bw_ds_to_hm);
    if (model_h > model_d) {

        gettimeofday(&st, NULL);
        crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);
        gettimeofday(&et, NULL);

        exc_time = simulation_time_us(st, et);

        fp->stat.op_host_count++;
        fp->stat.total_host_exec_time += exc_time;

        if (fp->stat.op_host_count >= stat_interval) {
            fp->stat.avg_host_exec_time = fp->stat.total_host_exec_time / fp->stat.op_host_count;
            fp->stat.op_host_count = 0;
            fp->stat.total_host_exec_time = 0;
        }

        // execute at host
        load_data_to_host(fp, &cache_req);
        /* Put checksum into write block */
        memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

        if (cache_insert(fp->inode, (char *)p, fp->off, count, fp,
                    &fp->inode->cache_tree, &cache_req, CACHE_WRITE_OP) < 0) {
            printf("interval tree insertion fail\n");
            return -1;
        }
        
    } else {
        // execute at device
        offload_data_to_device(fp, &cache_req, &desc);
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                nvme_cmd_read_chksm_write_cache, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);

#if 0
        fp->stat.op_device_count++;

        if (fp->stat.op_device_count >= stat_interval) {
            fp->stat.avg_device_exec_time = fp->stat.total_device_exec_time / fp->stat.op_device_count;
            fp->stat.op_device_count = 0;
        }
#endif
    }

    retval = count;
    return (size_t)retval;
}

#endif

size_t devfs_readchksmpwrite_cache_hybrid(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

#ifdef CACHE_MODEL
        return devfs_readchksmpwrite_cache_hybrid_model(fd, p, count, offset, crc_pos); 
#endif
        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
        uint32_t crc = 0;
        struct cache_req cache_req;

        memset(&desc, 0, sizeof(struct macro_op_desc));
        memset(&cache_req, 0, sizeof(struct cache_req));
        cache_req.node_num = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        cache_read(fp->inode, fp, offset, count, (char *) p,
                                        &fp->inode->cache_tree, &cache_req);
    
        if (cache_req.node_num <= 0 ) {
            return -1;
        }

        //printf("in_host_num: %ld, in_device_num: %ld, in_storage_num: %ld\n", 
        //        cache_req.in_host_num, cache_req.in_device_num, cache_req.in_storage_num);

        if (cache_req.in_host_num > cache_req.in_device_num + cache_req.in_storage_num) {
            // execute at host
            retval = load_data_to_host(fp, &cache_req);

            crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);

            /* Put checksum into write block */
            memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);
            if (cache_insert(fp->inode, (char *)p, fp->off, count, fp,
                        &fp->inode->cache_tree, NULL, CACHE_WRITE_OP) < 0) {
                printf("interval tree insertion fail\n");
                return -1;
            }

        } else {
            // execute at device
            offload_data_to_device(fp, &cache_req, &desc);

            retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                    nvme_cmd_read_chksm_write_cache, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);
        }

        //printf("devfs_readchksmpwrite_cache_hybrid, offset: %ld, count: %ld, retval: %ld\n", offset, count, retval);
        if (retval < 0) {
            printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
            return -1;
        }

        return (size_t)retval;
}

size_t devfs_readchksmpwrite_cache_host(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
        uint32_t crc = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        /* printf("checksumpwrite_cache, fd: %d, offset: %ld, count: %d\n", fd, offset, count); */
        if ((retval = cache_read(fp->inode, fp, offset, count, (char *) p,
                                        &fp->inode->cache_tree, NULL))) {
                hit_counter++;
        }

        if (retval > 0) {

                /* printf("hit\n"); */
                /* Calculate checksum */
                crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);

		        /* Put checksum into write block */
                memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);
                if (cache_insert(fp->inode, (char *)p, slba, count, fp,
                                        &fp->inode->cache_tree, NULL, CACHE_WRITE_OP) < 0) {
                        printf("interval tree insertion fail\n");
                        return -1;
                }
        } else {


                if ((retval = crfs_pread(fd, (void *)p, count, offset)) != count) {
                        printf("File data block checksum write fail, retval: %d \n", retval);
                        return -1;
                }

                /* Calculate checksum */
                crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);

                /* Put checksum into write block */
                memcpy((void *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

                if (cache_insert(fp->inode, (char *)p, slba, count, fp,
                                        &fp->inode->cache_tree, NULL, CACHE_WRITE_OP) < 0) {
                        printf("interval tree insertion fail\n");
                        return -1;
                }
        }
        if (retval < 0){
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return (size_t)retval;
}

size_t devfs_readchksmpwrite(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        /* Build I/O vec */

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_read_chksm_write, (void *)p, slba, (u64)count, crc_pos, NULL, NULL);

        if (retval < 0){
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        return (size_t)retval;
}


size_t devfs_checksumpwrite_cache_host(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct device_req dev_req;
        int i = 0;
        uint32_t crc = 0;

        dev_req.dev_req_num = 0;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

        /* Calculate checksum */
        crc = crc32(MAGIC_SEED, p, count - CHECKSUMSIZE);

        /* Put checksum into write block */
        memcpy((char *)p + count - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

        /* printf("checksumpwrite_cache, fd: %d, offset: %ld, count: %d\n", fd, offset, count); */
        if (cache_insert(fp->inode, (char *)p, slba, count, fp,
                                &fp->inode->cache_tree, NULL, CACHE_WRITE_OP) < 0) {
                printf("interval tree insertion fail\n");
                return -1;
        }

        retval = count;
            
#if 0
        if (dev_req.dev_req_num > 0) {

                /* printf("host cache is full, write device cache,num:%d, slba: %ld, nlb: %ld, offset: %ld\n",  */
                                /* dev_req.dev_req_num, dev_req.slba[i], dev_req.nlb[i], dev_req.slba[i] - slba); */

                retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                                nvme_cmd_write_chksm, (void *)p, slba, (u64)count, crc_pos, NULL);
        }
#endif

        if (retval < 0){
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        return (size_t)retval;

}


size_t devfs_checksumpwrite(int fd, const void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

#if defined(_DEBUG)
        printf("devfs_write writing at %zu offset \n", count);
#endif

        /* Build I/O vec */

        desc = checksumpwrite_cmd(p, count, offset);

#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_write_chksm, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, slba, (u64)count, crc_pos, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_checksumpwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return (size_t)retval;
}

/*Append write to existing file position*/
size_t devfs_checksumread(int fd, void *p, size_t count, uint8_t crc_pos) {

        u64 slba = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        // mark as -1 to indicate this is a read call
        // instead of a pread
        slba = INVALID_SLBA;

#if defined(_DEBUG)
        printf("***devfs_read reading at %zu offset \n", count);
        printf("***devfs_read unvme_do_devfs_io called \n");
#endif

        /* Build I/O vec */
        desc = checksumread_cmd(p, count);

#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_read_chksm, p, slba, (u64)count, crc_pos, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, p, slba, (u64)count, crc_pos, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_checksumread failed fd = %d, pos = %lu, count = %lu \n", fd, slba, count);
                return -1;
        }

        //printf("count = %d, offset = %d\n", count, fp->off);

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_read at %zu retval \n", retval);
#endif
        return (size_t)retval;
}

size_t devfs_checksumpread(int fd, void *p, size_t count, off_t offset, uint8_t crc_pos) {

        u64 slba = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

#if defined(_DEBUG)
        printf("***devfs_read reading at %zu offset \n", count);
        printf("***devfs_read unvme_do_devfs_io called \n");
#endif

        /* Build I/O vec */
        desc = checksumpread_cmd(p, count, offset);


#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_read_chksm, p, slba, (u64)count, crc_pos, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, p, slba, (u64)count, crc_pos, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_checksumpread failed fd = %d, pos = %lu, count = %lu \n", fd, slba, count);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_read at %zu retval \n", retval);
#endif
        return (size_t)retval;
}

/* Leveldb related Compound POSIX Calls */
size_t leveldb_checksumwrite(int fd, const void* data, size_t data_len, char* meta,
		size_t meta_len, int checksum_pos, int type_pos, uint8_t end, int cal_type, uint32_t type_crc) {

#ifndef _USE_VECTOR_IO_CMD
	size_t count = data_len + meta_len;
	char *buf = NULL;
	uint32_t crc = 0;

	buf = crfs_malloc(count);
	if (buf == NULL) {
		fprintf(stderr, "malloc fail\n");
		return -1;
	}

	if (end) {
		memcpy(buf, data, data_len);
		memcpy(buf + data_len, meta, meta_len);
	} else {
		memcpy(buf, meta, meta_len);
		memcpy(buf + meta_len, data, data_len);
	}

	size_t r = devfs_checksumwrite(fd, buf, data_len + meta_len, end);

	if (r != data_len + meta_len) {
		crfs_free(buf);
		return -errno;
	}
	crfs_free(buf);

	return r;

#else

	u64 slba = 0;
	int r;
    	size_t count = 0;
    	char *buf = NULL;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	slba = fp->off;

	count = data_len + meta_len;
	buf = crfs_malloc(count);
	if (buf == NULL) {
		fprintf(stderr, "malloc fail\n");
		return -1;
	}

        desc.num_op = 4;
        if (end) {
                memcpy(buf, data, data_len);
                memcpy(buf + data_len, meta, meta_len);
                desc.iov[0].opc = nvme_cmd_write_buffer;
                desc.iov[0].prp = (uint64_t)buf;
                desc.iov[0].slba = 0;
                desc.iov[0].nlb = count - 4;
                desc.iov[1].opc = nvme_cmd_chksm;
                desc.iov[1].prp = MACRO_VEC_NA;
                desc.iov[1].slba = 0;
                desc.iov[1].nlb = data_len;
                desc.iov[2].opc = nvme_cmd_write_buffer;
                desc.iov[2].prp = MACRO_VEC_PREV;
                desc.iov[2].slba = count - 4;
                desc.iov[2].nlb = MACRO_VEC_PREV;
                desc.iov[3].opc = nvme_cmd_append;
                desc.iov[3].prp = MACRO_VEC_NA;
                desc.iov[3].slba = INVALID_SLBA;
                desc.iov[3].nlb = count;

        } else {
                memcpy(buf, meta, meta_len);
                memcpy(buf + meta_len, data, data_len);
                desc.iov[0].opc = nvme_cmd_write_buffer;
                desc.iov[0].prp = (uint64_t) buf;
                desc.iov[0].slba = 0;
                desc.iov[0].nlb = count;
                desc.iov[1].opc = nvme_cmd_chksm;
                desc.iov[1].prp = MACRO_VEC_NA;
                desc.iov[1].slba = meta_len;
                desc.iov[1].nlb = data_len;
                desc.iov[2].opc = nvme_cmd_write_buffer;
                desc.iov[2].prp = MACRO_VEC_PREV;
                desc.iov[2].slba = 0;
                desc.iov[2].nlb = MACRO_VEC_PREV;
                desc.iov[3].opc = nvme_cmd_append;
                desc.iov[3].prp = MACRO_VEC_NA;
                desc.iov[3].slba = INVALID_SLBA;
                desc.iov[3].nlb = count;
        }

	r = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)(data_len + meta_len), end, &desc, NULL);

	//printf("leveldb_checksumwrite fd = %d, count: %d, ret: %d, end: %d, fp->off: %d, fname: %s\n", fd, count, r, end, fp->off, fp->fname);
	if (r < 0){
                printf("leveldb_checksumwrite failed fd = %d, %d\n", fd, r);
                crfs_free(buf);
		return -1;
	}

	fp->off += r;
    	crfs_free(buf);
	return r;
#endif	//_USE_VECTOR_IO_CMD

	return r;
}

ssize_t leveldb_checksumread(int fd, void *buf, size_t count, uint8_t end) {

#ifndef _USE_VECTOR_IO_CMD
	size_t r = devfs_checksumread(fd, buf, count, end);
	//printf("devfs_checksumread, return: %d\n", r);
	if (r < 0) {
		return -errno;
	}
	return r;

#else

	u64 slba = 0;
	int r;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	slba = fp->off;

	/* Build I/O vec */
	desc.num_op = 3;
	if (end) {
		desc.iov[0].opc = nvme_cmd_read;
		desc.iov[0].prp = (uint64_t)buf;
		desc.iov[0].slba = INVALID_SLBA;
		desc.iov[0].nlb = count;
		desc.iov[1].opc = nvme_cmd_chksm;
		desc.iov[1].prp = MACRO_VEC_NA;
		desc.iov[1].slba = 0;
		desc.iov[1].nlb = count - 5;
		desc.iov[2].opc = nvme_cmd_match;
		desc.iov[2].prp = MACRO_VEC_PREV;
		desc.iov[2].addr = count - 4;
		desc.iov[2].nlb = MACRO_VEC_PREV;
	} else {
		desc.iov[0].opc = nvme_cmd_read;
		desc.iov[0].prp = (uint64_t)buf;
		desc.iov[0].slba = INVALID_SLBA;
		desc.iov[0].nlb = count;
		desc.iov[1].opc = nvme_cmd_leveldb_log_chksm;
		desc.iov[1].prp = MACRO_VEC_NA;
		desc.iov[1].slba = 7;
		desc.iov[1].nlb = MACRO_VEC_NA;;
		desc.iov[2].opc = nvme_cmd_match;
		desc.iov[2].prp = MACRO_VEC_PREV;
		desc.iov[2].addr = 0;
		desc.iov[2].nlb = MACRO_VEC_PREV;
	}

	r = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			//nvme_cmd_append_chksm, (void *)p, nlb, (u64)count, crc_pos, &desc);
			nvme_cmd_compound, (void *)buf, slba, (u64)count, end, &desc, NULL);

//	printf("leveldb_checksumread fd = %d, end: %d, offset: %d, count: %d, ret: %d\n", fd, end, slba, count, r);
	if (r < 0){
		printf("leveldb_checksumread failed fd = %d, %d\n", fd, r);
		return -1;
	}

	fp->off += r;

	return r;
#endif	//_USE_VECTOR_IO_CMD
	
}

ssize_t leveldb_checksumpread(int fd, void *buf, size_t count, off_t offset, uint8_t end) {
#ifndef _USE_VECTOR_IO_CMD
	size_t r = devfs_checksumpread(fd, buf, count, offset, end);
	//printf("devfs_checksumpread, return: %d\n", r);
	if (r < 0) {
		return -errno;
	}
	return r;

#else

	u64 slba = 0;
	int r;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd);
		return -1;
	}

	slba = fp->off;

	desc = leveldb_checksumpread_cmd(buf, count, end, offset);

	r = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_compound, (void *)buf, offset, (u64)count, end, &desc, NULL);

	if (r < 0){
		printf("leveldb_checksumpread failed fd = %d, %d\n", fd, r);
		return -1;
	}

	fp->off += r;
	return r;
#endif	//_USE_VECTOR_IO_CMD
}

size_t devfs_compresswrite_cache(int fd, const void *p, size_t count, char* in_file) {
#ifndef COMPRESS_CACHE
    return 0;
#else

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        size_t retval = 0;
        int input_fd, output_fd = 0;
        struct macro_op_desc desc;
        char out_fname[256];
        uinode *target = NULL;
        ufile *fp = NULL;
        char *output = NULL;
        size_t outsz = 0;

        printf("compresswrite, in_file:%s, size: %ld\n", in_file, count);
        // search in libfs cache
        input_fd = crfs_open_file(in_file, O_RDWR, FILEPERM);
        if (input_fd < 0) {
            printf("failed to open file %d\n", input_fd);
                return -1;
        }
        fp = &ufile_table.open_files[input_fd];

        if ((retval = cache_read(fp->inode, fp, 0, count, (char *) p,
                                        &fp->inode->cache_tree, NULL))) {
                hit_counter++;
        }

        crfs_close_file(input_fd);

        if (retval > 0) {
            // compress
            
            output = (char *)crfs_malloc(count* 2);
            if (snappy_compress(&g_snappy_env, (const char *)p, count, output, &outsz) != 0) {
                printf("compress failed\n");
            }

            if (outsz) {
                bzero(out_fname, 256);
                strcat(out_fname, OUTPUT_DIR);
                strcat(out_fname, in_file);
                strcat(out_fname, ".comp");

                output_fd = crfs_open_file(out_fname, O_RDWR, FILEPERM);
                crfs_write(output_fd, output, outsz);
                crfs_close_file(output_fd);
            }
            crfs_free(output);
        } else {

            fp = &ufile_table.open_files[fd];
            if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
            }

#if defined(_DEBUG)
            printf("devfs_compress, count: %d \n", count);
#endif

            desc.num_op = 1;
            desc.iov[0].opc = nvme_cmd_open;
            desc.iov[0].prp = (u64)in_file;
            desc.iov[0].slba = 0;
            desc.iov[0].nlb = strlen(in_file);

            retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                    nvme_cmd_compress_write, (void *)p, slba, (u64)count, 0, &desc, NULL);

            if (retval < 0){
                printf("devfs_write failed %ld\n", retval);
                return -1;
            }

            fp->off += retval;
        }

#if defined(_DEBUG)
        printf("devfs_compress, count: %d, ret: %d \n", count, retval);
#endif
        return retval;
#endif
}

size_t devfs_compresswrite(int fd, const void *p, size_t count, char* in_file) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;
        char *output_file = "/mnt/ram/output/test";

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_compress, count: %d \n", count);
#endif
#if 1
        desc.num_op = 1;
        desc.iov[0].opc = nvme_cmd_open;
        desc.iov[0].prp = (u64)in_file;
        desc.iov[0].slba = 0;
        desc.iov[0].nlb = strlen(in_file);
#else
        desc.num_op = 5;

        desc.iov[0].opc = nvme_cmd_read;
        desc.iov[0].prp = MACRO_VEC_NA;
        desc.iov[0].slba = 0;
        desc.iov[0].nlb = count;
        desc.iov[1].opc = nvme_cmd_open;
        desc.iov[1].prp = (u64)out_file;
        desc.iov[1].slba = 0;
        desc.iov[1].nlb = strlen(out_file);
        desc.iov[2].opc = nvme_cmd_compress;
        desc.iov[2].prp = MACRO_VEC_NA;
        desc.iov[2].slba = 0;
        desc.iov[2].nlb = count;
        desc.iov[3].opc = nvme_cmd_write;
        desc.iov[3].prp = MACRO_VEC_PREV;
        desc.iov[3].slba = 0;
        desc.iov[3].nlb = MACRO_VEC_PREV;
        desc.iov[4].opc = nvme_cmd_close;
        desc.iov[4].prp = MACRO_VEC_NA;
        desc.iov[4].slba = INVALID_SLBA;
        desc.iov[4].nlb = MACRO_VEC_NA;
#endif
#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
            nvme_cmd_compress_write, (void *)p, slba, (u64)count, 0, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, MACRO_VEC_NA, slba, (u64)count, 0, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_write failed %d\n", retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_compress, count: %d, ret: %d \n", count, retval);
#endif
        return retval;
}

size_t devfs_encryptwrite(int fd, const void *buf, size_t count) {
        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write writing at %zu offset \n", count);
#endif

        slba = fp->off;

        /* Build I/O vec */
        desc = encryptwrite_cmd(buf, count);

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)count, 0, &desc, NULL);

        if (retval < 0){
                printf("devfs_encryptwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return retval;

}

size_t devfs_encryptpwrite(int fd, const void *buf, size_t count, off_t offset) {
        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write writing at %zu offset \n", count);
#endif

        slba = fp->off;

        /* Build I/O vec */
        desc = encryptpwrite_cmd(buf, count, offset);

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)count, 0, &desc, NULL);

        if (retval < 0){
                printf("devfs_encryptwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return retval;
}

size_t devfs_preadencryptpwrite(int fd, const void *buf, size_t count, off_t offset) {
        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write writing at %zu offset \n", count);
#endif

        slba = fp->off;

        /* Build I/O vec */
        desc = preadencryptpwrite_cmd(buf, count, offset);

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)count, 0, &desc, NULL);

        if (retval < 0){
                printf("devfs_encryptwrite failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return retval;
}

size_t devfs_open_pread_close(int fd, const void *p, size_t count, off_t offset, char* filename) {

        u64 slba = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

        slba = offset;

/* #if defined(_DEBUG) */
        /* printf("devfs_open_read_close, fd: %d, count: %d, offset: %d, filename: %s \n", fd, count, offset, filename); */
/* #endif */
        desc.num_op = 1;

        desc.iov[0].opc = nvme_cmd_open;
        desc.iov[0].prp = (u64)filename;
        desc.iov[0].slba = 0;
        desc.iov[0].nlb = strlen(filename);

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
            nvme_cmd_open_pread_close, (void *)p, slba, (u64)count, 0, &desc, NULL);


        /* printf("devfs_open_read_close, fd: %d, count: %d, offset: %d, filename: %s, ret: %d \n", fd, count, offset, filename, retval); */
        if (retval < 0){
                printf("devfs_open_pread_close failed %d\n", retval);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_open_read_close, count: %d, ret: %d \n", count, retval);
#endif
        return retval;
}

size_t devfs_open_pwrite_close_batch(int fd, const void **p, size_t count, off_t* offsets, char* out_file, size_t batch_size) {

        u64 slba = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_open_pwrite_close: %d \n", count);
#endif

        desc.num_op = batch_size;
        for (int i = 0; i < batch_size; i++) {
                desc.iov[i].opc = nvme_cmd_open_write_close;
                desc.iov[i].prp = (uint64_t)p[i];
                desc.iov[i].slba = 0;
                desc.iov[i].nlb = count;
        }

        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
            nvme_cmd_open_write_close_batch, (void *)out_file, slba, (u64)strlen(out_file), 0, &desc, NULL);

        if (retval < 0){
                printf("devfs_write failed %d\n", retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_open_pwrite_close, count: %d, ret: %d \n", count, retval);
#endif
        return retval;
}

size_t devfs_open_pwrite_close(int fd, const void *p, size_t count, off_t offset, char* out_file) {

        u64 slba = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_open_pwrite_close: %d \n", count);
#endif
        desc.num_op = 4;

        desc.iov[0].opc = nvme_cmd_open;
        desc.iov[0].prp = (u64)out_file;
        desc.iov[0].slba = 0;
        desc.iov[0].nlb = strlen(out_file);
        desc.iov[1].opc = nvme_cmd_write_buffer;
        desc.iov[1].prp = (u64)p;
        desc.iov[1].slba = 0;
        desc.iov[1].nlb = count;
        desc.iov[2].opc = nvme_cmd_write;
        desc.iov[2].prp = MACRO_VEC_NA;
        desc.iov[2].slba = offset;
        desc.iov[2].nlb = count;
        desc.iov[3].opc = nvme_cmd_close;
        desc.iov[3].prp = MACRO_VEC_NA;
        desc.iov[3].slba = INVALID_SLBA;
        desc.iov[3].nlb = MACRO_VEC_NA;

#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
            nvme_cmd_open_write_close, (void *)p, slba, (u64)count, 0, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, MACRO_VEC_NA, slba, (u64)count, 0, &desc, NULL);
#endif

        if (retval < 0){
                printf("devfs_write failed %d\n", retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_open_pwrite_close, count: %d, ret: %d \n", count, retval);
#endif
        return retval;
}

ssize_t devfs_read_append(int fd, const void *p, size_t count) {

	u64 slba = 0; 
	size_t write_cnt = 0, left = count;
	int written = 0; 
	int retval;
	struct macro_op_desc desc;

	ufile *fp = &ufile_table.open_files[fd];
	if (!fp) {
		printf("failed to get ufile %d\n", fd); 
		return -1;
	}    

	slba = fp->off;

        /* printf("readappend, fd: %d, count: %d, offset: %d \n", fd, count, slba); */

	/* Build I/O vec */
	desc = readmodifyappend_cmd(p, count);

	retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
			nvme_cmd_read_append, (void *)p, slba, (u64)count, 0, &desc, NULL);

        /* printf("readappend, fd: %d, count: %d, offset: %d, ret: %d \n", fd, count, slba, retval); */

	if (retval < 0){
	        printf("devfs_readappend failed fd = %d, %d\n", fd, retval);
		return -1;
	}    
        fp->off += retval;

	return (size_t)retval;
}

size_t crfs_checksumwritecc(int fd, const void *p, size_t count, int crc_pos) {

        u64 slba = 0;
        size_t write_cnt = 0, left = count;
        int written = 0;
        int retval;
        struct macro_op_desc desc;

        ufile *fp = &ufile_table.open_files[fd];
        if (!fp) {
                printf("failed to get ufile %d\n", fd);
                return -1;
        }

#if defined(_DEBUG)
        printf("devfs_write writing at %zu offset \n", count);
#endif

        slba = fp->off;

        /* Build I/O vec */
        desc = checksumwritecc_cmd(p, count);

#ifndef _USE_VECTOR_IO_CMD
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_append_chksm, (void *)p, slba, (u64)count, crc_pos, &desc, NULL);
#else
        retval = unvme_do_devfs_io_macro(g_dev, fd, fp->fd_queue.vsq,
                        nvme_cmd_compound, (void *)MACRO_VEC_NA, INVALID_SLBA, (u64)count, crc_pos, &desc, NULL);
#endif
        if (retval < 0){
                printf("devfs_checksumwritecc failed fd = %d, %d\n", fd, retval);
                return -1;
        }

        fp->off += retval;

#if defined(_DEBUG)
        printf("devfs_write wrote at %zu retval \n", retval);
#endif
        return retval;
}

/* Inject Crash calls */
int crfs_injectcrash(int fd, int crash_code) {
        int ret = 0;
        struct vfio_crfs_inject_crash_cmd map;

        map.argsz = sizeof(map);
        map.crash_pos = (u32)crash_code;

        ret = ioctl(g_dev, VFIO_DEVFS_INJECT_CRASH_CMD, &map);
        if (ret < 0) {
                fprintf(stderr,"ioctl VFIO_DEVFS_INJECT_CRASH_CMD for %d "
                                "failed errno %d \n", crash_code, errno);
                return -errno;
        }

        return ret;
}
