#ifndef CXL_MEM_H
#define CXL_MEM_H

#include "cache.h"
#include "crfslibio.h"

struct cxl_mem {
	void *mem;
	int bitmap[CXL_MEM_PG_NUM];
	int head;
	crfs_mutex_t lock;
	crfs_spinlock_t spinlock;
};

struct cxl_mem_namespace {
    int ns_start;
    int ns_end;
    int free;
    int head;
};

struct cxl_mem_namespace_map {
    struct cxl_mem_namespace* ns_map;
    int cxl_ns_size;
    int head;
	crfs_mutex_t lock;
};

#ifdef CXL_MEM
extern struct cxl_mem cxl_mempool;
extern struct cxl_mem_namespace_map cxl_mem_ns_map;
#endif

void* cxl_mem_init(int dev, unsigned int num_pages, unsigned int cxl_ns_size);

struct cxl_mem_namespace* cxl_ns_malloc();

void cxl_ns_free(struct cxl_mem_namespace *cxl_ns);

int cxl_cache_insert(uinode *inode, struct req_tree_entry *node, void* buf, unsigned long start, unsigned long end);

int cxl_cache_read(struct req_tree_entry *node, void* buf, unsigned long start, unsigned long end);

int cxl_cache_rw(uinode *inode, int cache_rw_op, struct req_tree_entry *tree_node, void *buf, 
                unsigned long start, unsigned long end);


void cxl_free(void *q_addr);
int cxl_mem_idx(void *q_addr);

#endif
