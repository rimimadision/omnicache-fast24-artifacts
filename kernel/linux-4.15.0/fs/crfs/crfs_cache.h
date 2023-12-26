#ifndef _LINUX_DEVFS_CACHE_H
#define _LINUX_DEVFS_CACHE_H

#include <linux/fs.h>
#include <linux/devfs.h>
#include <linux/file.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/vfio.h>
#include <linux/time.h>
#include <linux/lz4.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/skcipher.h>
#include <linux/vmalloc.h>

#define KB (1024UL)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define BLOCK_SIZE (4096)
#define NODE_SIZE_LIMIT (2 * 1024 * 1024)

#define CACHE_READ_OP 1
#define CACHE_WRITE_OP 2
#define CACHE_APPEND_OP 3

#define CXL_MEM_PG_NUM 2048 
#define CXL_MEM_PG_SIZE (2*1024*1024UL)
extern __u64 cxl_user_addr;
extern __u64 cxl_mem_map[CXL_MEM_PG_NUM];



/*
 *  * the result after insert or read from interval tree
 *   */
struct tree_rw_req {
        void* buf; 
        unsigned long start;
        unsigned long end;
        unsigned long buf_off;

};

int cache_evict(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw, unsigned long slba, unsigned long nlb, struct rb_root *root);

#ifdef GLOBAL_INTERVAL_TREE
int cache_insert(nvme_cmdrw_t *cmdrw, void* buf, unsigned long start, unsigned long end);
int cache_read(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw, nvme_cmdrw_t *cmdrw_mop, void* buf, unsigned long start, unsigned long end);
int cache_rw(struct crfss_fstruct *rd, int cache_rw_op, nvme_cmdrw_t *cmdrw, nvme_cmdrw_t *cmdrw_mop, void* buf,
                unsigned long start, unsigned long end);
#else
int cache_read(struct inode* inode, unsigned long slba, unsigned long nlb, char *buf,
               struct rb_root *root);


int cache_insert(struct inode *inode, void* blk_addr, unsigned long slba, unsigned long nlb,
                 struct rb_root *root);
#endif

#endif
