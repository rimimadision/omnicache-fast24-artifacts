#include <sys/mman.h>
#include <sys/ioctl.h>


#include "cxl_mem.h"
#include "vfio.h"
#include "unvme_nvme.h"
#include "time_delay.h"

struct cxl_mem cxl_mempool;

struct cxl_mem_namespace_map cxl_mem_ns_map;

void* cxl_mem_init(int dev, unsigned int num_pages, unsigned int cxl_ns_size) {

	void *bufAddr;
	int ret;
#ifdef CXL_MEM_NAMESPACE
    if(num_pages == 0 || cxl_ns_size == 0) {
#else
    if(num_pages == 0) {
#endif
        printf("cxl_mem_init failed\n");
        return NULL;
    } 

	size_t req_size = ((CXL_MEM_PG_SIZE) * (num_pages+1));

	posix_memalign(&bufAddr, PAGE_SIZE , req_size);
	mlock(bufAddr, req_size);

	if(bufAddr == NULL) {
		fprintf(stderr,"cxl buffer create failed \n");
		return NULL;
	}

    printf("memalign successful, req_size: %ld\n", req_size);

	memset(bufAddr, 0, req_size);
	struct vfio_iommu_type1_queue_map map = {
			.argsz = sizeof(map),
			.flags = (VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE),
			.vaddr = (__u64)bufAddr,
			.iova = (__u64)0, //dev->iova,
			.size = (__u64)0,
			.qpfnlist = (__u64)0,
			.qpfns = (__u64)num_pages,
	};

	if (ioctl(dev, VFIO_IOMMU_GET_CXL_MEM_ADDR, &map) < 0) {
		fprintf(stderr,"ioctl cxl errno %d \n", errno);
		crfs_free(bufAddr);
		return NULL;
	}

    if (bufAddr == NULL) {
        printf("cxl memory init failed\n");
        exit(1);
    }

    cxl_mempool.mem = bufAddr;
	memset(cxl_mempool.bitmap, 0, CXL_MEM_PG_NUM*sizeof(int));
	cxl_mempool.head = 0;
	crfs_mutex_init(&cxl_mempool.lock);
	/* crfs_spin_init(&cxl_mempool.spinlock, 0); */

#ifdef CXL_MEM_NAMESPACE
    printf("allocate namespace, size: %d\n", cxl_ns_size);
    if (cxl_ns_size !=0) {
        cxl_mem_ns_map.ns_map = (struct cxl_mem_namespace *) malloc(cxl_ns_size * sizeof(struct cxl_mem_namespace));
        size_t pg_size_per_ns =  num_pages / cxl_ns_size;
        size_t ns_pos = 0;
        for (int i = 0; i < cxl_ns_size; i++) {
            cxl_mem_ns_map.ns_map[i].ns_start = ns_pos;
            cxl_mem_ns_map.ns_map[i].ns_end = ns_pos + pg_size_per_ns;
            cxl_mem_ns_map.ns_map[i].head= cxl_mem_ns_map.ns_map[i].ns_start;
            cxl_mem_ns_map.ns_map[i].free = 1;
            ns_pos += pg_size_per_ns;
        }
        cxl_mem_ns_map.cxl_ns_size = cxl_ns_size;
        cxl_mem_ns_map.head = 0;
	    crfs_mutex_init(& cxl_mem_ns_map.lock);
    }
#endif
	return bufAddr;
}

struct cxl_mem_namespace* cxl_ns_malloc() {

    struct cxl_mem_namespace *cxl_ns= NULL;
    printf("cxl ns malloc, ns size: %d\n", cxl_mem_ns_map.cxl_ns_size);

    crfs_mutex_lock(&cxl_mem_ns_map.lock);

    while (cxl_mem_ns_map.ns_map[cxl_mem_ns_map.head].free != 1)
        cxl_mem_ns_map.head = (cxl_mem_ns_map.head + 1) & (cxl_mem_ns_map.cxl_ns_size- 1);

    cxl_ns = &cxl_mem_ns_map.ns_map[cxl_mem_ns_map.head];
    cxl_ns->free = 0;
    cxl_mem_ns_map.head = (cxl_mem_ns_map.head + 1) & (cxl_mem_ns_map.cxl_ns_size- 1);

    crfs_mutex_unlock(&cxl_mem_ns_map.lock);
    printf("cxl ns malloc done, ns_start: %d, ns_end: %d, head: %d\n", cxl_ns->ns_start, cxl_ns->ns_end, cxl_ns->head);
    return cxl_ns;
}

void cxl_ns_free(struct cxl_mem_namespace *cxl_ns) {
	int ns_idx = (cxl_ns - cxl_mem_ns_map.ns_map) / sizeof(struct cxl_mem_namespace);
    cxl_mem_ns_map.ns_map[ns_idx].free = 1;
}

void* cxl_malloc(unsigned int num_pages, struct cxl_mem_namespace *cxl_ns) {
	void *bufAddr = NULL;
    /* printf("cxl malloc \n"); */
#ifdef CXL_MEM_NAMESPACE
    int head = cxl_ns->head;
	crfs_mutex_lock(&cxl_mempool.lock);
    while (cxl_mempool.bitmap[head] != 0) {
        head = (head + 1 >= cxl_ns->ns_end? cxl_ns->ns_start:head+1);
    }
	cxl_mempool.bitmap[head] = 1;
	bufAddr = cxl_mempool.mem + head * CXL_MEM_PG_SIZE;
    cxl_ns->head = (head + 1 >= cxl_ns->ns_end? cxl_ns->ns_start:head+1);
	crfs_mutex_unlock(&cxl_mempool.lock);
#else
        crfs_mutex_lock(&cxl_mempool.lock);
        /* crfs_spin_lock(&cxl_mempool.spinlock); */
    while (cxl_mempool.bitmap[cxl_mempool.head] != 0)
        cxl_mempool.head = (cxl_mempool.head + 1) & (CXL_MEM_PG_NUM - 1);
    cxl_mempool.bitmap[cxl_mempool.head] = 1;
    bufAddr = cxl_mempool.mem + cxl_mempool.head * CXL_MEM_PG_SIZE;
    cxl_mempool.head = (cxl_mempool.head + 1) & (CXL_MEM_PG_NUM- 1);
        /* crfs_spin_unlock(&cxl_mempool.spinlock); */
        crfs_mutex_unlock(&cxl_mempool.lock);
#endif

    /* printf("cxl malloc done\n"); */
	return bufAddr;
}

void cxl_free(void *q_addr) {
    /* printf("cxl free\n"); */
	int idx = (q_addr - cxl_mempool.mem) / CXL_MEM_PG_SIZE;
	cxl_mempool.bitmap[idx] = 0;
}

int cxl_mem_idx(void *q_addr) {
	int idx = (q_addr - cxl_mempool.mem) / CXL_MEM_PG_SIZE;
    return idx;
}

int cxl_cache_insert(uinode *inode, struct req_tree_entry *node, void* buf, unsigned long start, unsigned long end) {
        int ret = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;
        if (node == NULL) {
            ret = -1;
            return ret;
        }
        /* printf("cxl cache insert, start: %ld, end: %ld\n", start, end); */
        if ((node->cxl_buf_start == 0 && node->cxl_buf_end == 0) || node->cxl_blk_addr == NULL) {
            /* cxl_malloc returns a fix sized block from the cxl memory pool */
            node->cxl_blk_addr = (void*)cxl_malloc(NODE_SIZE_LIMIT, inode->cxl_mem_ns);
            node->cxl_buf_start = start;
            node->cxl_buf_end = end;
            node->size = NODE_SIZE_LIMIT;
            cache_size += NODE_SIZE_LIMIT;
            __sync_add_and_fetch(&dev_cache_size, NODE_SIZE_LIMIT);
        }

        node_buffer_offset = start - node->it.start;
        copy_size = end - start + 1;

        memcpy((void *)node->cxl_blk_addr + node_buffer_offset,
                (void *)buf, copy_size);

        ret = copy_size;

        if (start < node->cxl_buf_start) {
            node->cxl_buf_start = start;
        }

        if (end > node->cxl_buf_end) {
            node->cxl_buf_end = end;
        }

        /* printf("cxl cache insert, start: %ld, end: %ld, ret: %ld\n", start, end, ret); */
        return ret;
}

int cxl_cache_read(struct req_tree_entry *node, void* buf, unsigned long start, unsigned long end) {

        int ret = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;

        if (node == NULL) {
                ret = -1;
                return ret;
        }

        /* printf("cxl cache read, start: %ld, end: %ld\n", start, end); */
        node_buffer_offset = start - node->it.start;
        copy_size = end - start + 1;

        if (start >= node->cxl_buf_start && node->cxl_buf_end >= end)  {
                memcpy(buf, (void *)node->cxl_blk_addr + node_buffer_offset,
                                copy_size);
                ret += (end - start + 1);
        }

        /* printf("cxl read data, start: %ld, end: %ld, buf_s: %ld, buf_e: %ld, ret: %d\n", start, end, node->cxl_buf_start, node->cxl_buf_end, ret); */
        return ret;
}


int cxl_cache_rw(uinode *inode, int cache_rw_op, struct req_tree_entry *tree_node, void *buf, 
                unsigned long start, unsigned long end) {
        int retval = 0;
        if (buf == NULL) {
                printf("buffer is NULL\n");
                retval = -1;
                goto exit_node_rw;
        }
        switch (cache_rw_op) {
                case CACHE_WRITE_OP:
                case CACHE_APPEND_OP:
                        retval = cxl_cache_insert(inode, tree_node, buf,
                                        start, end);
                        break;
                case CACHE_READ_OP:
                        retval = cxl_cache_read(tree_node, buf, 
                                        start, end);
                        break;  
                default:
                        printf("unsupport cache op\n");
                        retval = -1;
                        goto exit_node_rw;
        }
exit_node_rw:
        return retval;

}
