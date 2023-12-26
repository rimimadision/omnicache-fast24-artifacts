#ifndef CRFSCACHE_H
#define CRFSCACHE_H

#include <pthread.h>
#include "list.h"
#include "crfslibio.h"
#include "thpool.h"
#include "unvme_nvme.h"

//#define CACHE_SIZE (32 * 1024 * 1024)
#define KB (1024UL)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define BLOCK_SIZE (4096)
#define NODE_SIZE_LIMIT (2 * 1024 * 1024)

#define MAX_CACHE_NODE 8

#define CACHE_READ_OP 1
#define CACHE_WRITE_OP 2
#define CACHE_APPEND_OP 3
#define CACHE_EVICT_OP 4

/*
 * Data places 
 */
#define IN_HOST_CACHE 1
#define IN_DEVICE_CACHE 2
#define IN_DEVICE_STORAGE 3

/* LRU list head */
extern struct list_head lru_list_head;
extern struct list_head lru_list_tail;
extern struct list_head closed_file_head;
extern struct list_head closed_file_tail;

extern unsigned long dev_cache_size;

static unsigned long lru_size = 0;
extern unsigned long cache_size;
extern unsigned long hit_counter;
extern unsigned long CACHE_LIMIT;
extern unsigned long HOST_CACHE_LIMIT;
extern unsigned long DEV_CACHE_LIMIT;

extern int stopeviction;

extern crfs_mutex_t lru_closed_files_list_lock;
extern crfs_mutex_t lru_interval_nodes_list_lock;
extern threadpool cache_thpool;

struct cache_insert_arg {
        ufile *fp;
        char* buf;
        unsigned long nlb;
        unsigned long slba;
};

#define DEVICE_CACHE_IO 0
#define DEVICE_DIRECT_IO 1

struct cache_req {
    int node_num;

    unsigned long in_host_num;
    unsigned long in_device_num;
    unsigned long in_storage_num;

    struct req_tree_entry *nodes[MAX_CACHE_NODE];

    unsigned long start[MAX_CACHE_NODE];
    unsigned long end[MAX_CACHE_NODE];
    void* bufs[MAX_CACHE_NODE];
};

struct device_req {

    int dev_req_num;

    unsigned long slba[MAX_CACHE_NODE];
    unsigned long nlb[MAX_CACHE_NODE];
    void* bufs[MAX_CACHE_NODE];
};

/*
 * the result after insert or read from interval tree
 */
struct tree_rw_req {
        void* buf; 
        unsigned long start;
        unsigned long end;
        unsigned long buf_off;
};

int evict_node();
int start_evict();
int do_evict();

int interval_node_rw(ufile *fp, int cache_rw_op,struct req_tree_entry *tree_node, void *buf, 
                unsigned long start, unsigned long end);

int load_data_to_host(ufile *fp, struct cache_req* req);
int offload_data_to_device(ufile *fp, struct cache_req* req, struct macro_op_desc *desc);

int cache_read(uinode *inode, ufile *fp, unsigned long slba, unsigned long nlb, char* buf, 
                struct rb_root *root, struct cache_req* cache_req);
int cache_insert(uinode *inode, void* blk_addr, unsigned long slba, unsigned long nlb, ufile *fp, 
        struct rb_root *root, struct cache_req* cache_req, int cache_rw_op); 
int flush_inode_cache(int fd, ufile *fp, uinode *inode, struct rb_root *root);

void drop_all_cache(uinode **all_inodes, long all_files_cnt);
#endif
