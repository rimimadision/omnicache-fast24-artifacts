#include "cache.h"
#include "utils.h"
#include "cxl_mem.h"

unsigned long hit_counter = 0;
unsigned long cache_size = 0;
unsigned long host_cache_size = 0;
unsigned long dev_cache_size = 0;
unsigned long CACHE_LIMIT = 0;
unsigned long HOST_CACHE_LIMIT = 0;
unsigned long DEV_CACHE_LIMIT = 0;


struct list_head lru_list_head = LIST_HEAD_INIT(lru_list_head);
struct list_head lru_list_tail = LIST_HEAD_INIT(lru_list_tail);
struct list_head closed_file_head = LIST_HEAD_INIT(closed_file_head);
struct list_head closed_file_tail = LIST_HEAD_INIT(closed_file_tail);

crfs_mutex_t lru_closed_files_list_lock = CRFS_MUTEX_INITIALIZER;
crfs_mutex_t lru_interval_nodes_list_lock = CRFS_MUTEX_INITIALIZER;

threadpool cache_thpool;
int is_flushing = 0;

nvme_command_rw_t *io_cmd_batch = NULL;
int cmd_cnt = 0;



int dev_cache_rw(struct req_tree_entry *node, ufile *fp, void* buf, unsigned long start, unsigned long end, int rw) {
        void *ioq = NULL;
        int retval = 0;
        int idx = 0;
        int opc = 0;
        nvme_command_rw_t *cmdrw = NULL;

        if (node == NULL) {
                printf("tree node is NULL\n");
                retval = -1;
                goto exit_populate_dev_cache_node;
        }

        switch (rw) {
            case CACHE_READ_OP:
                opc = nvme_cmd_read_cache;
                break;
            case CACHE_APPEND_OP:
            case CACHE_WRITE_OP:
                opc = nvme_cmd_write_cache;
                break;
            case CACHE_EVICT_OP:
                opc = nvme_cmd_evict_cache;
                break;
        }

        /* Get the submission head of FD-queue */
        ioq = find_avail_vsq_entry(fp);
        nvme_cmd_rw_new(opc, CID, NSID, start, end - start + 1, (u64)buf, ioq);
        cmdrw = (nvme_command_rw_t *)ioq;

        cmdrw->blk_addr = (u64) node->blk_addr; 
        cmdrw->buf_start = node->buf_start;
        cmdrw->buf_end = node->buf_end;

        if (node->blk_addr == NULL) {
            if (node->in_lru_list == 0) {
                add_to_lru_list(&node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock);
                node->in_lru_list = 1;
            }
            __sync_add_and_fetch(&dev_cache_size, NODE_SIZE_LIMIT);
        }

        retval = vfio_crfs_queue_write(0, fp->fd, ioq, 1);

        node->blk_addr = (void *)cmdrw->blk_addr;
        node->buf_start = cmdrw->buf_start;
        node->buf_end = cmdrw->buf_end;
 
exit_populate_dev_cache_node:
        return retval;
}

int read_data_from_interval_node(ufile *fp, struct req_tree_entry *node, void *buf,
                                 unsigned long start, unsigned long end) {
        int ret = 0;
        unsigned long node_cmd_start = 0;
        unsigned long node_cmd_end = 0;
        unsigned long cmd_start_bound = 0;
        unsigned long cmd_end_bound = 0;
        unsigned long nlb = end - start;
        int idx_start = 0;
        int idx_end = 0;
        unsigned long copy_size = 0;
        unsigned long buffer_offset = 0;
        unsigned long cmd_buffer_offset = 0;
        unsigned long node_buffer_offset = 0;
        nvme_command_rw_t cmd;
        nvme_command_rw_t *io_cmd = NULL;

        if (node == NULL) {
                ret = -1;
                return ret;
        }

#ifdef  CACHE_PAGE_GRANULARITY
        idx_start = (start - node->it.start) / BLOCK_SIZE;
        idx_end = (end - node->it.start) / BLOCK_SIZE;

        crfs_mutex_lock(&node->lock);

        if (node->in_lru_list == 0) {
            add_to_lru_list(&node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock);
            node->in_lru_list = 1;
        }

#ifdef PERFOPT
	crfs_mutex_unlock(&node->lock);
#endif

        if (node->blk_addr == NULL) {
            node->blk_addr = (void*)malloc(NODE_SIZE_LIMIT);
            __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
            __sync_add_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
#ifdef PERFOPT
	    crfs_mutex_lock(&node->lock);
#endif
            node->size += NODE_SIZE_LIMIT;
#ifdef PERFOPT
	    crfs_mutex_unlock(&node->lock);
#endif
        }


        for (int i = idx_start; i <= idx_end; i++) {

                cmd = node->cmds[i];
                cmd_start_bound = node->it.start + i * BLOCK_SIZE ;
                cmd_end_bound = node->it.start + (i + 1) * BLOCK_SIZE - 1;

                if (cmd.nlb == 0) {

                    cmd.slba  = cmd_start_bound;
                    cmd.nlb  = BLOCK_SIZE;

                    io_cmd  = gen_io_cmd(fp, (void *)node->blk_addr + (cmd_start_bound - node->it.start),
                            nvme_cmd_read, cmd.slba, BLOCK_SIZE);

                    if (io_cmd == NULL) {
                        printf("io cmd is null\n");
                    }

                    /* printf("miss, fd: %d, fname: %s, read data cmd, slba: %ld, count: %ld, idx: %d\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, i); */
                    vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
                    /* printf("miss, fd: %d, fname: %s, read data cmd, slba: %ld, count: %ld, idx: %d, done\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, i); */

                }
                
                node_cmd_start = cmd.slba;
                node_cmd_end = cmd.slba + cmd.nlb - 1;
                cmd_buffer_offset = start - node->it.start;

                if (start >= node_cmd_start && node_cmd_end >= end) {


                        /* printf("read command hit1, start: %ld, end: %ld, nlb: %d, cmd start: %ld, cmd end: %ld, cmd_size: %d\n",   */
                                /* start, end, nlb, node_cmd_start, node_cmd_end, node->cmd_size);  */

#ifdef PERFOPT			
			crfs_mutex_lock(&node->lock);
#endif

                        memcpy(buf + buffer_offset,
                               (void *)node->blk_addr + cmd_buffer_offset,
                               end - start + 1);
#ifdef PERFOPT
			crfs_mutex_unlock(&node->lock);
#endif

                        ret += (end - start + 1);
                        start = end = 0;

                } else if(start >= node_cmd_start && start <= node_cmd_end &&
                                node_cmd_end < end)  {
                        /* printf("read command hit2, start: %ld, end: %ld, nlb: %d, cmd start: %ld, cmd end: %ld, cmd_size: %d\n",   */
                                /* start, end, nlb, node_cmd_start, node_cmd_end, node->cmd_size);  */

                        copy_size = node_cmd_end - start + 1;

#ifdef PERFOPT
                        crfs_mutex_lock(&node->lock);
#endif			
                        memcpy(buf + buffer_offset,
                               (void *)node->blk_addr + cmd_buffer_offset,
                               copy_size);
#ifdef PERFOPT
                        crfs_mutex_unlock(&node->lock);
#endif
                        ret += (copy_size);
                        start = node_cmd_end + 1; 
                        buffer_offset += copy_size;
                }
        }

#ifndef PERFOPT
        crfs_mutex_unlock(&node->lock);
#endif


#else
        crfs_mutex_lock(&node->lock);
        if (node->size <= 0 || node->blk_addr == NULL) {
            
#ifdef NEARCACHE_OPT
            /* printf("nearcache opt\n"); */
            if (dev_cache_size < DEV_CACHE_LIMIT) {
                /* printf("nearcache opt, devcache: %ld\n", dev_cache_size); */
                node->buf_start = start;
                node->buf_end = end;
                node->size = NODE_SIZE_LIMIT;
                //cache_size += NODE_SIZE_LIMIT;
                __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                __sync_add_and_fetch(&dev_cache_size, NODE_SIZE_LIMIT);

                /* printf("nvme_cmd_read_nearcache\n"); */

                io_cmd  = gen_io_cmd(fp, (void *)node->blk_addr,
                        nvme_cmd_read_nearcache, start, end);

                if (io_cmd == NULL) {
                    printf("io cmd is null\n");
                }

                io_cmd->common.prp2 = buf;
                ret += vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);

                node->cache_in_kernel = 1;
                goto exit_read_from_interval_node;
            } else {  
#endif
                node->blk_addr = (void*)malloc(NODE_SIZE_LIMIT);
                node->buf_start = start;
                node->buf_end = end;
                node->size = NODE_SIZE_LIMIT;
                //cache_size += NODE_SIZE_LIMIT;
                __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                __sync_add_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
                io_cmd  = gen_io_cmd(fp, (void *)node->blk_addr,
                        nvme_cmd_read, node->it.start, NODE_SIZE_LIMIT);

                if (io_cmd == NULL) {
                    printf("io cmd is null\n");
                }
                vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
                if (node->in_lru_list == 0) {
                    add_to_lru_list(&node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock);
                    node->in_lru_list = 1;
                }
#ifdef NEARCACHE_OPT
            }
#endif
        }

        if (node->size != 0 && (void*)node->blk_addr!=NULL) {
            node_buffer_offset = start - node->it.start;

            if (start >= node->buf_start && node->buf_end >= end)  {

                copy_size = end - start + 1; 
                memcpy(buf, (void *)node->blk_addr + node_buffer_offset,
                        copy_size);
                ret += copy_size;

            }  else if (start  >= node->buf_start && start <= node->buf_end  && end > node->buf_end) {

                copy_size = node->buf_end - start + 1; 
                memcpy(buf, (void *)node->blk_addr + node_buffer_offset,
                        copy_size);
                ret += (copy_size);

            }
        }
exit_read_from_interval_node:
        crfs_mutex_unlock(&node->lock);
#endif // block GRANULARITY
        return ret;
}

int insert_cmd_to_interval_node(ufile *fp, struct req_tree_entry *node, char *buf,
                                unsigned long start, unsigned long end, int cache_op) {
        int ret = 0;
        unsigned long cmd_start = 0;
        unsigned long cmd_end = 0;
        unsigned long nlb = end - start;
        unsigned long copy_size = 0;

        unsigned long buffer_offset = 0;
        unsigned long cmd_start_bound = 0;
        unsigned long cmd_end_bound = 0;
        unsigned long node_buffer_offset = 0;
        int idx_start = 0, idx_end = 0;
        int i = 0;
        nvme_command_rw_t *io_cmd = NULL;
        if (node == NULL) {
                ret = -1;
                return ret;
        }

        idx_start = (start - node->it.start) / BLOCK_SIZE;
        idx_end = (end - node->it.start) / BLOCK_SIZE;
        crfs_mutex_lock(&node->lock);

#ifdef  CACHE_PAGE_GRANULARITY
        /* printf("insert_cmd_to_interval_node, start: %ld, end: %ld, node_s: %ld, node_e: %ld, idx_start:%d, idx_end:%d\n",  */
                /* start, end, node->it.start, node->it.last,idx_start, idx_end); */
        
        if (node->in_lru_list == 0) {
            add_to_lru_list(&node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock);
            node->in_lru_list = 1;
        }

        if (node->blk_addr == NULL) {
            node->blk_addr = (void*)malloc(NODE_SIZE_LIMIT);
            __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
            __sync_add_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
            node->size += NODE_SIZE_LIMIT;
        }

        for (int i = idx_start; i <= idx_end; i++) {
                cmd_start_bound = node->it.start + i * BLOCK_SIZE ;
                cmd_end_bound = node->it.start + (i + 1) * BLOCK_SIZE - 1;
                
                /* printf("insert_cmd_to_interval_node, idx: %d, cmd nlb: %d, start: %ld, end: %ld, cmd_start_bound: %ld, cmd_end_bound: %ld, cmd_start: %ld, cmd_end: %ld\n", i, node->cmds[i].nlb, start, end, cmd_start_bound, cmd_end_bound, node->cmds[i].slba, node->cmds[i].slba + node->cmds[i].nlb - 1); */

                copy_size = end > cmd_end_bound? cmd_end_bound - start + 1: end - start + 1;
                node_buffer_offset = start - node->it.start;               

                if (node->cmds[i].nlb <= 0) {

                    node->cmds[i].slba  = cmd_start_bound;
                    node->cmds[i].nlb  = BLOCK_SIZE;

                    if (cache_op == CACHE_WRITE_OP) {
                        // read data from storage
                        io_cmd  = gen_io_cmd(fp, (void *)node->blk_addr + (cmd_start_bound - node->it.start),
                                nvme_cmd_read, node->cmds[i].slba, BLOCK_SIZE);

                        if (io_cmd == NULL) {
                            printf("io cmd is null\n");
                        }
                        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
                        /* printf("block miss, read from storage, cache size: %ld\n", cache_size); */
                    }
                }

                if ((void*)node->blk_addr == NULL || buf == NULL) {
                        printf("error, buffer is empty");
                        goto exit_insert_node;
                }

                /* printf("memcpy, node_buf_off: %ld, buf_off: %ld, copy_size: %ld\n",  */
                        /* node_buffer_offset, buffer_offset, copy_size); */

                memcpy((void *)node->blk_addr + node_buffer_offset,
                       (void *)buf + buffer_offset, copy_size);

                cmd_start = node->cmds[i].slba;
                cmd_end = node->cmds[i].slba + node->cmds[i].nlb - 1;

#if 0
                if (start + copy_size - 1 > cmd_end) {
                        node->cmds[i].nlb += start + copy_size - cmd_end - 1;
                }
                
                if (start < cmd_start) {
                        node->cmds[i].nlb += cmd_start - start;
                        node->cmds[i].slba = start; 
                }
#endif
                buffer_offset += copy_size;
                start += copy_size;
                ret += copy_size;
        }
#else
        debug_printf("insert_cmd_to_interval_node, start: %ld, end: %ld, node_s: %ld, node_e: %ld, idx_start:%d, idx_end:%d\n", 
                start, end, node->it.start, node->it.last,idx_start, idx_end);
        if (node->size <= 0 || node->blk_addr == NULL) {
            node->blk_addr = (void*)malloc(NODE_SIZE_LIMIT);
            node->buf_start = start;
            node->buf_end = end;
            node->size = NODE_SIZE_LIMIT;
            //cache_size += NODE_SIZE_LIMIT;
            __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
            __sync_add_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);

#if 1
            if (cache_op == CACHE_WRITE_OP) {
                // read data from storage
                io_cmd  = gen_io_cmd(fp, (void *)node->blk_addr,
                        nvme_cmd_read, node->it.start, NODE_SIZE_LIMIT);

                if (io_cmd == NULL) {
                    printf("io cmd is null\n");
                }
                vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
                /* printf("block miss, read from storage, cache size: %ld\n", cache_size); */
            }
#endif
            if (node->in_lru_list == 0) {
                add_to_lru_list(&node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock);
                node->in_lru_list = 1;
            }
        }

        node_buffer_offset = start - node->it.start;
        copy_size = end - start + 1; 

        memcpy((void *)node->blk_addr + node_buffer_offset,
                (void *)buf, copy_size);

        ret += copy_size;

        if (start < node->buf_start) {
            node->buf_start = start;
        }

        if (end > node->buf_end) {
            node->buf_end = end;
        }
#endif // block GRANULARITY

        debug_printf("insert command, start: %ld, end: %ld, nlb: %d, node start: %ld, node end: %ld, cmd_size: %ld\n",  
                        start, end, nlb, node->it.start, node->it.last, node->cmd_size); 
exit_insert_node:
        crfs_mutex_unlock(&node->lock);
        return ret;
}

struct req_tree_entry *create_new_interval_node(ufile *fp, unsigned long start, unsigned long last) {
        struct req_tree_entry *new_tree_node = NULL;
        new_tree_node = crfs_malloc(sizeof(struct req_tree_entry));
        if (!new_tree_node) {
                printf("Failed to allocate interval tree node\n");
                return NULL;
        }

        memset(new_tree_node, 0, sizeof(struct req_tree_entry));
        new_tree_node->cmd_size = 0;
        new_tree_node->data_size = 0;
        new_tree_node->cache_in_kernel = 0;

        new_tree_node->cxl_buf_start = 0;
        new_tree_node->cxl_buf_end = 0;

        crfs_mutex_init(&new_tree_node->lock);
        new_tree_node->fp = fp;
        new_tree_node->it.start = start;
        new_tree_node->it.last = last;

        new_tree_node->in_lru_list = 0;
        return new_tree_node;
}

void cache_count(struct cache_req* cache_req, int place, unsigned long bytes_num) {
    if (cache_req == NULL) return;
    switch (place) {
        case IN_HOST_CACHE:
            cache_req->in_host_num += bytes_num;
            break;
        case IN_DEVICE_CACHE:
            cache_req->in_device_num += bytes_num;
            break;
        case IN_DEVICE_STORAGE:
            cache_req->in_device_num += bytes_num;
            break;
        default:
            break;
    }
}

int load_data_to_host(ufile *fp, struct cache_req* req) {
    if (req == NULL || req->node_num <= 0) return -1;
    int retval = 0;

    struct req_tree_entry *tree_node = NULL;

    for (int i = 0; i< req->node_num;i++) {
        tree_node = req->nodes[i];

        if (tree_node->cache_in_kernel) {
#ifdef CXL_MEM
            retval = cxl_cache_rw(fp->inode, CACHE_READ_OP, tree_node, req->bufs[i], req->start[i], req->end[i]);
#else
            retval = dev_cache_rw(tree_node, fp, req->bufs[i], req->start[i], req->end[i], CACHE_READ_OP);
#endif
        } else {
            retval = interval_node_rw(fp, CACHE_READ_OP, tree_node, 
                    req->bufs[i], req->start[i], req->end[i]);
        }
    }

    return retval;
}

int offload_data_to_device(ufile *fp, struct cache_req* req, struct macro_op_desc *desc) {
    if (req == NULL || req->node_num <= 0) return -1;

    struct req_tree_entry *tree_node = NULL;
    int j = 0;
    int retval = 0;

    for (int i = 0; i< req->node_num;i++) {
        tree_node = req->nodes[i];

        if (tree_node->cache_in_kernel) {
            j = desc->num_op;
            if (tree_node->blk_addr == NULL) {
                desc->iov[j].opc = nvme_cmd_read;
                desc->iov[j].prp = (uint64_t)NULL;
                desc->iov[j].slba = req->start[i];
                desc->iov[j].nlb = req->end[i] - req->start[i] + 1;
            } else {
                desc->iov[j].opc = nvme_cmd_read_cache;
                desc->iov[j].prp = (uint64_t)tree_node->blk_addr;
                desc->iov[j].slba = req->start[i];
                desc->iov[j].nlb = req->end[i] - req->start[i] + 1;
            }
            desc->num_op ++;
        } else {

            interval_node_rw(fp, CACHE_READ_OP, tree_node, 
                    req->bufs[i], req->start[i], req->end[i]);
        }
    }
    return retval;
}



int populate_cache_request(struct cache_req* cache_req, void* buf, unsigned long start, unsigned long end, 
            struct req_tree_entry *tree_node) {
        int retval = 0;
        int idx = 0;
        unsigned long nlb = 0;
        if (cache_req == NULL) {
                /* printf("dev_req is NULL\n"); */
                retval = -1;
                goto exit_populate_dev_cache_node;
        }

        idx = cache_req->node_num;

        cache_req->nodes[idx] = tree_node;

        cache_req->start[idx] = start;
        cache_req->end[idx] = end;
        cache_req->bufs[idx] = buf;
        cache_req->node_num++;

        nlb = end - start + 1;

        if (tree_node->blk_addr == NULL) {
            cache_count(cache_req, IN_DEVICE_STORAGE, nlb);
        } else if (tree_node->cache_in_kernel) {
            cache_count(cache_req, IN_DEVICE_CACHE, nlb);
        } else {
            cache_count(cache_req, IN_HOST_CACHE, nlb);
        }
        /* printf("cache is full, slba: %ld, nlb: %ld\n", slba, nlb); */

exit_populate_dev_cache_node:
        return retval;
}


int populate_dev_request(struct device_req* dev_req, void* buf, unsigned long start, unsigned long end) {
        int retval = 0;
        int idx = 0;
        if (dev_req == NULL) {
                /* printf("dev_req is NULL\n"); */
                retval = -1;
                goto exit_populate_dev_cache_node;
        }

        idx = dev_req->dev_req_num;

        dev_req->slba[idx] = start;
        dev_req->nlb[idx] = end - start + 1;
        dev_req->bufs[idx] = buf;
        dev_req->dev_req_num++;

        /* printf("cache is full, slba: %ld, nlb: %ld\n", slba, nlb); */

exit_populate_dev_cache_node:
        return retval;
}

int interval_node_rw(ufile *fp, int cache_rw_op,struct req_tree_entry *tree_node, void *buf, 
                unsigned long start, unsigned long end) {
        int retval = 0;
        /* printf("interval_node_rw\n"); */
        if (buf == NULL) {
                printf("buffer is NULL\n");
                retval = -1;
                goto exit_node_rw;
        }
        switch (cache_rw_op) {
                case CACHE_WRITE_OP:
                        retval = insert_cmd_to_interval_node(fp, tree_node, buf,
                                        start, end, cache_rw_op);
                        tree_node->dirty = 1;
                        break;
                case CACHE_APPEND_OP:
                        retval = insert_cmd_to_interval_node(fp, tree_node, buf,
                                        start, end, cache_rw_op);
                        break;
                case CACHE_READ_OP:
                        retval = read_data_from_interval_node(fp, tree_node, buf, 
                                        start, end);
                        break;  
                default:
                        printf("unsupport cache op: %d\n", cache_rw_op);
                        retval = -1;
                        goto exit_node_rw;
        }
exit_node_rw:
        return retval;

}

void redirct_request_to_device(struct req_tree_entry *tree_node, struct cache_req* cache_req) {
    if (tree_node != NULL) {
        if (tree_node->blk_addr == NULL &&  __sync_fetch_and_add(&dev_cache_size, 0)>= DEV_CACHE_LIMIT) {
            tree_node->cache_in_kernel = 1;
        }
    }
}

int interval_tree_rw(ufile *fp, struct rb_root *root, struct tree_rw_req* tree_rw, 
                struct cache_req* cache_req, int cache_rw_op) {

        int retval = 0;
        int cache_ret = 0;
        struct interval_tree_node *it = NULL;
        struct req_tree_entry *tree_node = NULL;

        void* buf = tree_rw->buf;
        unsigned long start = tree_rw->start;
        unsigned long end = tree_rw->end;
        unsigned long buf_off = 0;

        /* printf("interval_tree_rw, start: %ld, end: %ld\n", start, end); */

        if (root == NULL) {
            printf("root is null\n");
        }

        it = interval_tree_iter_first(root, start, end);

        while (it) {
                tree_node = container_of(it, struct req_tree_entry, it);
                if (!tree_node) {
                        printf("Get NULL object!\n");
                        retval = -1;
                        goto exit_iterate_tree;
                }

                if (start >= it->start && it->last >= end) {
                        debug_printf("hit1, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld, inkernel: %ld\n", start,
                                end, it->start, it->last, tree_node->cache_in_kernel); 

                        if (cache_req != NULL) {
                            populate_cache_request(cache_req, buf + buf_off, start, end, tree_node);
                        } else {
#ifdef CACHE_HYBRID
                            redirct_request_to_device(tree_node, cache_req);
#endif

                            if (!tree_node->cache_in_kernel) {
                                cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                        buf + buf_off, start, end);
                            } else {
#ifdef CXL_MEM
                                    if (tree_node->cxl_blk_addr  == NULL && dev_cache_size >= DEV_CACHE_LIMIT) {
                                            tree_node->cache_in_kernel = 0;
                                            cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                                            buf + buf_off, start, end);
                                    } else {
                                            cache_ret = cxl_cache_rw(fp->inode, cache_rw_op, tree_node, buf + buf_off, start, end);
                                    }
#else
                                retval += dev_cache_rw(tree_node, fp, buf + buf_off, start, end, cache_rw_op);
#endif
                            }
                        }

                        retval += cache_ret;
                        start = end = 0;
                        lru_list_update(&tree_node->lru_node, &lru_list_tail,&lru_interval_nodes_list_lock);
                        break;
                } else if (start >= it->start && end > it->last)   {
                        debug_printf("hit2, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld, inkernel: %ld\n", start,
                                end, it->start, it->last, tree_node->cache_in_kernel); 

                        if (cache_req != NULL) {
                            populate_cache_request(cache_req, buf + buf_off, start, it->last, tree_node);
                        } else {

#ifdef CACHE_HYBRID
                            redirct_request_to_device(tree_node, cache_req);
#endif
                            if (!tree_node->cache_in_kernel) {
                                cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                        buf + buf_off, start, it->last);
                            } else {
#ifdef CXL_MEM
                                    if (tree_node->cxl_blk_addr  == NULL && dev_cache_size >= DEV_CACHE_LIMIT) {
                                            tree_node->cache_in_kernel = 0;
                                            cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                                            buf + buf_off, start, it->last);

                                    } else {
                                        cache_ret = cxl_cache_rw(fp->inode, cache_rw_op, tree_node, buf + buf_off, start, it->last);
                                    }
#else
                                retval += dev_cache_rw(tree_node, fp, buf + buf_off, start, it->last, cache_rw_op);
#endif
                            }

                        }
                        retval += cache_ret;

                        buf_off += (it->last - start + 1);
                        start = it->last + 1;
                        lru_list_update(&tree_node->lru_node, &lru_list_tail,&lru_interval_nodes_list_lock);
                } else if (it->start > start && end <= it->last) {

                        debug_printf("hit3, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld\n", start,
                                end, it->start, it->last); 

                        if (cache_req != NULL) {
                            populate_cache_request(cache_req, buf + buf_off + (it->start - start), it->start, end, tree_node);
                        } else {

#ifdef CACHE_HYBRID
                            redirct_request_to_device(tree_node, cache_req);
#endif

                            if (!tree_node->cache_in_kernel) {
                                cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                        buf + buf_off + (it->start - start), it->start, end);
                            } else {
#ifdef CXL_MEM
                                    if (tree_node->cxl_blk_addr  == NULL && dev_cache_size >= DEV_CACHE_LIMIT) {
                                            tree_node->cache_in_kernel = 0;
                                            cache_ret = interval_node_rw(fp, cache_rw_op, tree_node, 
                                                            buf + buf_off + (it->start - start), it->start, end);
                                    } else {
                                            retval += cxl_cache_rw(fp->inode, cache_rw_op, tree_node, buf + buf_off + (it->start - start), it->start, end);
                                    }
#else
                                retval += dev_cache_rw(tree_node, fp, buf + buf_off + (it->start - start), it->start, end, cache_rw_op);
                                /* populate_dev_request(dev_req, buf + buf_off + (it->start - start), it->start, end); */
#endif
                            }

                        }
                        retval += cache_ret;
                        end = it->start - 1;
                        lru_list_update(&tree_node->lru_node, &lru_list_tail,&lru_interval_nodes_list_lock);
                }

                if ((start == 0 & end == 0) || start > end) {
                        break;
                }
find_next:
                it = interval_tree_iter_next(it, start, end);
        }

exit_iterate_tree:
        tree_rw->start = start;
        tree_rw->end = end;
        tree_rw->buf_off = buf_off;
        return retval;
}

int insert_new_tree_node(struct rb_root *root, ufile *fp, struct tree_rw_req* tree_rw, 
                struct cache_req* cache_req, int cache_rw_op) {

        struct req_tree_entry *new_tree_node = NULL;
        int retval = 0;
        unsigned long new_node_start = 0;
        unsigned long new_node_end = 0;
        unsigned long start_idx = 0;
        unsigned long end_idx = 0;
        unsigned long i = 0;

        void* buf = tree_rw->buf;
        unsigned long start = tree_rw->start;
        unsigned long end = tree_rw->end;
        unsigned long buf_off = tree_rw->buf_off;

        start_idx = start / NODE_SIZE_LIMIT;
        end_idx = end / NODE_SIZE_LIMIT; 
        /* printf("no overlap, insert new, start: %ld, end: %ld, start_idx: %d, end_idx: %d, buf_off: %ld\n", start, end, start_idx, end_idx, buf_off);  */
        for (i = start_idx; i <= end_idx; i++) {
                new_node_start = i * NODE_SIZE_LIMIT;
                new_node_end = new_node_start + NODE_SIZE_LIMIT - 1;

                new_tree_node = create_new_interval_node(fp, new_node_start, new_node_end);
#ifdef CACHE_HYBRID
                if (CACHE_LIMIT!=0 && __sync_fetch_and_add(&host_cache_size, 0)>= HOST_CACHE_LIMIT) {
                    if (dev_cache_size < DEV_CACHE_LIMIT) {
                        new_tree_node->cache_in_kernel = 1;
                        __sync_add_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                        /* cache_size += NODE_SIZE_LIMIT; */
                        /* printf("dev cache size: %ld, list: %ld\n", dev_cache_size, DEV_CACHE_LIMIT); */

#ifdef CXL_MEM
                        retval += cxl_cache_insert(fp->inode, new_tree_node, buf + buf_off, start, 
                                        end > new_node_end? new_node_end: end);
#else
                        dev_cache_rw(new_tree_node, fp, buf + buf_off, start, 
                                        end > new_node_end? new_node_end: end, CACHE_APPEND_OP);
#endif

                        if (end > new_node_end) {
                                buf_off += (new_node_end - start + 1);
                                start = new_node_end + 1; 
                        }

                        goto insert_node;
                        
                    }
                }
#endif
                debug_printf("idx: %ld, new_node: %p, start: %ld, end: %ld,new_node_start: %ld, new_node_end: %ld\n", i,  new_tree_node, start, end, new_node_start, new_node_end); 
                if (end > new_node_end) {
                        retval += insert_cmd_to_interval_node(
                                        fp, new_tree_node, buf + buf_off, start, new_node_end, cache_rw_op);
                        //buf_off += (new_node_end - start + 1);
                        buf_off += (new_node_end - start + 1);
                        start = new_node_end + 1; 
                } else {
                        retval += insert_cmd_to_interval_node(
                                        fp, new_tree_node, buf + buf_off, start, end, cache_rw_op);
                }

insert_node:
                interval_tree_insert(&new_tree_node->it, root);

                /* add_to_lru_list(&new_tree_node->lru_node, &lru_list_tail, &lru_interval_nodes_list_lock); */

        }
        return retval;
}

int cache_read(uinode *inode, ufile *fp, unsigned long slba, unsigned long nlb, char *buf,
               struct rb_root *root, struct cache_req* cache_req) {
        int retval = 0;
        struct tree_rw_req tree_rw;

        unsigned long start = (slba == INVALID_SLBA? 0: slba);
        unsigned long end = slba + nlb - 1;
        unsigned long cache_size_upper_bound = CACHE_LIMIT + (CACHE_LIMIT/5)*4;


#if 1
        if(cache_size > cache_size_upper_bound ) {
                evict_interval_nodes();
        }
#endif

        tree_rw.buf = buf;
        tree_rw.start = start;
        tree_rw.end = end;

        /* printf("cache_read, fd:%d, slba: %ld, end: %ld, fname: %s\n", fp->fd, slba, end, fp->fname);  */
        crfs_rwlock_rdlock(&inode->cache_tree_lock);
        /* printf("cache_read, slba: %ld, end: %ld, fname: %s, get lock\n", slba, end, fp->fname);  */
        
        retval = interval_tree_rw(fp, root, &tree_rw, cache_req, CACHE_READ_OP);

        if(retval < 0) {
            printf("interval tree insertion failed, cache_read, slba: %ld, end: %ld, fname: %s\n", slba, end, fp->fname);
        }
#if 0
        if (tree_rw.start < tree_rw.end) {
                populate_dev_request(dev_req, buf + tree_rw.buf_off, tree_rw.start, tree_rw.end);
        }
#endif
err_write_submission_tree_search:
        crfs_rwlock_unlock(&inode->cache_tree_lock);
        /* printf("cache_read, slba: %ld, end: %ld, fname: %s, retval: %ld done\n", slba, end, fp->fname, retval);  */
        return retval;
}

int cache_insert(uinode *inode, void* blk_addr, unsigned long slba, unsigned long nlb, ufile *fp,
                 struct rb_root *root, struct cache_req* cache_req, int cache_rw_op) {
        int retval = 0;
        struct tree_rw_req tree_rw;
        unsigned long start = slba;
        unsigned long end = slba + nlb - 1;

        if (root == NULL) {
                printf("root is null\n");
        }

        unsigned long cache_size_upper_bound = CACHE_LIMIT + (CACHE_LIMIT/5)*4;


#if 1
        if(cache_size > cache_size_upper_bound ) {
                evict_interval_nodes();
        }
#endif

        tree_rw.buf = blk_addr;
        tree_rw.start = start;
        tree_rw.end = end;

        crfs_rwlock_wrlock(&inode->cache_tree_lock);
        /* printf("cache insert, slba: %ld, nlb: %ld, fname: %s\n", slba, nlb, fp->fname); */

        retval = interval_tree_rw(fp, root, &tree_rw, cache_req, cache_rw_op);

        if(retval < 0) {
                printf("interval tree insertion failed, cache insert, slba: %ld, nlb: %ld, fname: %s\n", slba, nlb, fp->fname);
                goto err_submission_tree_insert;
        }

        if (tree_rw.start < tree_rw.end) {
                retval += insert_new_tree_node(root, fp, &tree_rw, cache_req, cache_rw_op);
        }

#if 0
        if (CACHE_LIMIT!=0 && __sync_fetch_and_add(&cache_size, 0)>= CACHE_LIMIT) {
            if (__sync_lock_test_and_set(&is_flushing, 1) == 0) {
                /* printf("add to thpool, cachesize: %ld\n", cache_size); */
                thpool_add_work(cache_thpool, (void*)start_evict, NULL);
            }
        }
#endif

err_submission_tree_insert:
        /* printf("cache insert done, fname: %s, retval: %ld\n", fp->fname, retval); */
        crfs_rwlock_unlock(&inode->cache_tree_lock);
        return retval;
}

int do_evict_in_kernel(ufile *fp, struct req_tree_entry *tree_node) {
        nvme_command_rw_t *io_cmd = NULL;

        if (tree_node == NULL) goto exit_evict;

        /* printf("evict in kernel, start: %ld, nlb: %ld\n", tree_node->it.start, tree_node->it.last - tree_node->it.start+ 1); */

        if (tree_node->blk_addr != NULL)
                dev_cache_rw(tree_node, fp, NULL, tree_node->it.start, 
                                tree_node->it.last, CACHE_EVICT_OP);

        tree_node->blk_addr = NULL;
        tree_node->buf_start = 0;
        tree_node->buf_end = 0;
        tree_node->dirty = 0;
#if 0
        io_cmd  = gen_io_cmd(fp, NULL,
                        nvme_cmd_evict_cache, tree_node->it.start, tree_node->it.last - tree_node->it.start+ 1);

        if (io_cmd == NULL) {
                printf("io cmd is null\n");
        }
        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
#endif 
exit_evict:
        return 0;
}

int do_evict_cxl_in_kernel(ufile *fp, struct req_tree_entry *tree_node) {
        nvme_command_rw_t *io_cmd = NULL;

        if (tree_node == NULL) goto exit_evict;

        /* printf("evict in CXL, start: %ld, nlb: %ld\n", tree_node->it.start, tree_node->it.last - tree_node->it.start+ 1); */
        if (tree_node->cxl_blk_addr == NULL) {
            /* printf("cxl memory is empty\n"); */
            goto exit_evict;
        }
        
        /* io_cmd  = gen_io_cmd(fp, tree_node->cxl_blk_addr, */
                        /* nvme_cmd_evict_cxl_cache, tree_node->it.start, cxl_mem_idx(tree_node->cxl_blk_addr)); */

        io_cmd  = gen_io_cmd(fp, tree_node->cxl_blk_addr,
                        nvme_cmd_evict_cxl_cache, tree_node->it.start, NODE_SIZE_LIMIT);

        if (io_cmd == NULL) {
                printf("io cmd is null\n");
		goto exit_evict;
        }
        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
exit_evict:
        return 0;
}

int flush_tree_node(ufile *fp, struct req_tree_entry *tree_node) {
        int retval = 0;
        unsigned long cmd_start_bound = 0;
        nvme_command_rw_t *io_cmd = NULL;
        nvme_command_rw_t *cache_cmd = NULL;

        if (tree_node == NULL) return retval;;
        crfs_mutex_lock(&tree_node->lock);
#ifdef CACHE_HYBRID
        if (tree_node != NULL && tree_node->cache_in_kernel) {
#ifdef CXL_MEM

                do_evict_cxl_in_kernel(fp, tree_node);
                cxl_free(tree_node->cxl_blk_addr);
#else
                do_evict_in_kernel(fp, tree_node);
#endif
                /* cache_size -= NODE_SIZE_LIMIT; */
                __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                __sync_sub_and_fetch(&dev_cache_size, NODE_SIZE_LIMIT);
                goto exit_flush_tree_node;
        }
#endif

#ifdef CACHE_PAGE_GRANULARITY
        for (int i = 0; i < NODE_CMD_SIZE; i++) {
                cmd_start_bound = tree_node->it.start + i * BLOCK_SIZE;
                cache_cmd = &tree_node->cmds[i];
                if (cache_cmd->nlb > 0) {
                        cache_cmd->nlb=0;
                }
        }
        if (tree_node->blk_addr) {
#if 1
            io_cmd  = gen_io_cmd(fp, (void *)tree_node->blk_addr,
                    nvme_cmd_write, tree_node->it.start, NODE_SIZE_LIMIT);

            if (io_cmd == NULL) {
                printf("io cmd is null\n");
            }
            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
            vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld done\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */

#endif
            crfs_free((void *)tree_node->blk_addr);
            tree_node->blk_addr = NULL;
            __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
            __sync_sub_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
        }
        tree_node->size=0;
        tree_node->buf_start=0;
        tree_node->buf_end=0;
        if (tree_node->in_lru_list) {
            remove_from_lru_list(&tree_node->lru_node, &lru_interval_nodes_list_lock);
            tree_node->in_lru_list = 0;
        }

#else

        if (tree_node->size != 0 && tree_node->blk_addr != NULL) {

#ifdef CRFS_WB_CACHE  
#ifndef LEVELDB_CACHE 

                        io_cmd  = gen_io_cmd(fp, (void *)tree_node->blk_addr,
                                        nvme_cmd_write, tree_node->it.start, NODE_SIZE_LIMIT);

            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
                        if (io_cmd == NULL) {
                                printf("io cmd is null\n");
                        }
                        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld done\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
#endif
#endif
                        crfs_free((void *)tree_node->blk_addr);
                        tree_node->blk_addr = (void *) NULL;
                        /* cache_size -= tree_node->size; */
                        __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                        __sync_sub_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
                        tree_node->size=0;
                        tree_node->buf_start=0;
                        tree_node->buf_end=0;
                        if (tree_node->in_lru_list) {
                            /* printf("remove from list\n"); */
                            remove_from_lru_list(&tree_node->lru_node, &lru_interval_nodes_list_lock);
                            tree_node->in_lru_list = 0;
                        }
        }

#endif // block GRANULARITY

exit_flush_tree_node:
        crfs_mutex_unlock(&tree_node->lock);
        return retval;
}


int flush_tree_node_batch(ufile *fp, struct req_tree_entry *tree_node) {
        int retval = 0;
        unsigned long cmd_start_bound = 0;
        nvme_command_rw_t *io_cmd = NULL;
        nvme_command_rw_t *cache_cmd = NULL;

        if (tree_node == NULL) return retval;;
        crfs_mutex_lock(&tree_node->lock);
#ifdef CACHE_HYBRID
        if (tree_node != NULL && tree_node->cache_in_kernel) {
#ifdef CXL_MEM

                do_evict_cxl_in_kernel(fp, tree_node);
                cxl_free(tree_node->cxl_blk_addr);
#else
                do_evict_in_kernel(fp, tree_node);
#endif
                /* cache_size -= NODE_SIZE_LIMIT; */
                __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                __sync_sub_and_fetch(&dev_cache_size, NODE_SIZE_LIMIT);
                goto exit_flush_tree_node;
        }
#endif

#ifdef CACHE_PAGE_GRANULARITY
        for (int i = 0; i < NODE_CMD_SIZE; i++) {
                cmd_start_bound = tree_node->it.start + i * BLOCK_SIZE;
                cache_cmd = &tree_node->cmds[i];
                if (cache_cmd->nlb > 0) {
                        cache_cmd->nlb=0;
                }
        }
        if (tree_node->blk_addr) {
                if (io_cmd_batch == NULL) {

                        io_cmd_batch  = gen_io_cmd(fp, (void *)tree_node->blk_addr,
                                        nvme_cmd_write_batch, tree_node->it.start, NODE_SIZE_LIMIT);

                }
                if (io_cmd_batch == NULL) {
                        /* printf("io cmd is null\n"); */
                        goto free_node;
                }

                if (cmd_cnt < 16) {
                        io_cmd_batch->param_vec[cmd_cnt].cond_param.addr = tree_node->blk_addr;
                        io_cmd_batch->param_vec[cmd_cnt].data_param.slba = tree_node->it.start;
                        io_cmd_batch->param_vec[cmd_cnt].data_param.nlb = NODE_SIZE_LIMIT;
                        cmd_cnt++;
                } else {
                        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd_batch, 1);
                        cmd_cnt = 0;
                        io_cmd_batch = NULL;
                }
                /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
                /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld done\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */

free_node:
            crfs_free((void *)tree_node->blk_addr);
            tree_node->blk_addr = NULL;
            __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
            __sync_sub_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
        }
        tree_node->size=0;
        tree_node->buf_start=0;
        tree_node->buf_end=0;
        if (tree_node->in_lru_list) {
            remove_from_lru_list(&tree_node->lru_node, &lru_interval_nodes_list_lock);
            tree_node->in_lru_list = 0;
        }

#else

        if (tree_node->size != 0 && tree_node->blk_addr != NULL) {

#ifdef CRFS_WB_CACHE

                if (io_cmd_batch == NULL) {

                        io_cmd_batch  = gen_io_cmd(fp, (void *)tree_node->blk_addr,
                                        nvme_cmd_write_batch, tree_node->it.start, NODE_SIZE_LIMIT);

                }
                if (io_cmd_batch == NULL) {
                        /* printf("io cmd is null\n"); */
                        goto free_node;
                }

                if (cmd_cnt < 16) {
                        io_cmd_batch->param_vec[cmd_cnt].cond_param.addr = tree_node->blk_addr;
                        io_cmd_batch->param_vec[cmd_cnt].data_param.slba = tree_node->it.start;
                        io_cmd_batch->param_vec[cmd_cnt].data_param.nlb = NODE_SIZE_LIMIT;
                        cmd_cnt++;
                } else {
                        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd_batch, 1);
                        cmd_cnt = 0;
                        io_cmd_batch = NULL;
                }

#if 0
                        io_cmd  = gen_io_cmd(fp, (void *)tree_node->blk_addr,
                                        nvme_cmd_write, tree_node->it.start, NODE_SIZE_LIMIT);

            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
                        if (io_cmd == NULL) {
                                printf("io cmd is null\n");
                        }
                        vfio_crfs_queue_write(0, fp->fd, (void *)io_cmd, 1);
#endif
            /* printf("fd: %d, fname: %s, flush data cmd, slba: %ld, count: %ld, state: %ld done\n", fp->fd, fp->fname, io_cmd->slba, io_cmd->nlb, io_cmd->status); */
#endif
free_node:
                        crfs_free((void *)tree_node->blk_addr);
                        tree_node->blk_addr = (void *) NULL;
                        /* cache_size -= tree_node->size; */
                        __sync_sub_and_fetch(&cache_size, NODE_SIZE_LIMIT);
                        __sync_sub_and_fetch(&host_cache_size, NODE_SIZE_LIMIT);
                        tree_node->size=0;
                        tree_node->buf_start=0;
                        tree_node->buf_end=0;
                        if (tree_node->in_lru_list) {
                            /* printf("remove from list\n"); */
                            remove_from_lru_list(&tree_node->lru_node, &lru_interval_nodes_list_lock);
                            tree_node->in_lru_list = 0;
                        }
        }

#endif // block GRANULARITY

exit_flush_tree_node:
        crfs_mutex_unlock(&tree_node->lock);
        /* printf("flush tree node done\n"); */
        return retval;
}

int flush_fp(int fd, ufile *fp, uinode *inode, struct rb_root *root) {
        int retval = 0;
        unsigned long long filesize = 32*GB;
        struct req_tree_entry *tree_node = NULL;
        unsigned long cmd_start_bound = 0;
        nvme_command_rw_t *cache_cmd = NULL;

        struct interval_tree_node *it;
        if (inode == NULL || root == NULL) return;
        crfs_rwlock_wrlock(&inode->cache_tree_lock);

        it = interval_tree_iter_first(root, 0, filesize);
        /* printf("iter tree fname: %s\n", fp->fname); */
        while (it) {
                tree_node = container_of(it, struct req_tree_entry, it);
                if (!tree_node) {
                        printf("Get NULL object!\n");
                        retval = -1;
                        goto err_flush;

                }
                flush_tree_node(fp, tree_node);
                it = interval_tree_iter_next(it, 0, filesize);

                interval_tree_remove(&tree_node->it, &tree_node->fp->inode->cache_tree);

                if (tree_node->in_lru_list) {
                   remove_from_lru_list(&tree_node->lru_node, &lru_interval_nodes_list_lock);
                   tree_node->in_lru_list = 0;
                }
                crfs_free(tree_node);
        }
err_flush:
        crfs_rwlock_unlock(&inode->cache_tree_lock);
        return retval;
}

int start_evict() {
    /* printf("start evict, cache_size: %ld\n", cache_size); */
    if (CACHE_LIMIT!= 0 && __sync_fetch_and_add(&cache_size, 0) >= CACHE_LIMIT) {
        /* printf("start evict, cache_size: %ld\n", cache_size); */
        do_evict();
        /* printf("start evict done, cache_size: %ld\n", cache_size); */
        /* __sync_lock_release(&is_flushing); */

    }
    return 0;
}

void evict_closed_files() {
        int idx = 0;
        uinode *evict_inode = NULL;
        ufile *fp = NULL;
        /* printf("evict_closed_files\n"); */

        crfs_mutex_lock(&lru_closed_files_list_lock);
        if (closed_file_head.next != &closed_file_tail) {
                evict_inode = list_first_entry(&closed_file_head, uinode, close_node);
        }
        crfs_mutex_unlock(&lru_closed_files_list_lock);

        if (evict_inode == NULL) {
                /* printf("inode is empty\n"); */
                return;
        }

        /* evict entile inode cache */
        while (evict_inode->ufilp[idx] != NULL) {
                fp = evict_inode->ufilp[idx];
                /* printf("flush closed files, idx: %d, fname: %s\n", idx, fp->fname); */
                flush_fp(fp->fd, fp, fp->inode, &fp->inode->cache_tree);
                idx++;
        }
        /* if (evict_inode->close_node.prev != NULL && evict_inode->close_node.next!=NULL) */
            /* list_del(&evict_inode->close_node); */
        put_inode(evict_inode, NULL);
}

void evict_interval_nodes() {
        struct req_tree_entry *evict_tree_node = NULL;
	    uinode *inode = NULL;
        unsigned long cache_size_stop = cache_size * 0.9;

        if (stopeviction) return;
        /* while(cache_size >= cache_size_stop) { */

                crfs_mutex_lock(&lru_interval_nodes_list_lock);
                if (lru_list_head.next != &lru_list_tail) {
                        evict_tree_node = list_first_entry(&lru_list_head,
                                        struct req_tree_entry, lru_node);
                }
                crfs_mutex_unlock(&lru_interval_nodes_list_lock);

#ifdef CACHE_PAGE_GRANULARITY
                if (evict_tree_node == NULL) {
#else
                if (evict_tree_node == NULL || evict_tree_node->blk_addr == NULL) {
#endif
                        /* printf("tree node is empty\n"); */
                        return;
                }

                inode = evict_tree_node->fp->inode;
                /* printf("evict tree node, node: %p start: %ld, end: %ld, cache_tree: %p\n", evict_tree_node, evict_tree_node->it.start, evict_tree_node->it.last, &evict_tree_node->fp->inode->cache_tree); */

                crfs_rwlock_wrlock(&inode->cache_tree_lock);
                flush_tree_node_batch(evict_tree_node->fp, evict_tree_node);
                /* interval_tree_remove(&evict_tree_node->it, &inode->cache_tree); */

                /* crfs_free(evict_tree_node); */
                /* evict_tree_node = NULL; */

                /* printf("cachesize: %ld\n", cache_size); */

                crfs_rwlock_unlock(&inode->cache_tree_lock);
        /* } */
}


void drop_all_cache(uinode **all_inodes, long all_files_cnt){
    struct req_tree_entry *evict_tree_node = NULL;
    uinode *inode = NULL;
    uinode *evict_inode = NULL;
    ufile *fp = NULL;
    printf("drop all cache, cnt: %d\n", all_files_cnt);
    int idx = 0;

    for (int i = 0; i< all_files_cnt;i++){
        idx = 0;

        evict_inode = all_inodes[i];
        //printf ("drop cache, idx: %d\n", i);
        if (evict_inode == NULL)  break;

        /* evict entile inode cache */
        if (evict_inode->ufilp[idx] != NULL) {
                fp = evict_inode->ufilp[idx];
                /* printf("flush closed files, idx: %d, fname: %s\n", idx, fp->fname); */
                flush_fp(fp->fd, fp, fp->inode, &fp->inode->cache_tree);
        }

#if 0
        /* evict entile inode cache */
        while (evict_inode->ufilp[idx] != NULL) {
                fp = evict_inode->ufilp[idx];
                /* printf("flush closed files, idx: %d, fname: %s\n", idx, fp->fname); */
                flush_fp(fp->fd, fp, fp->inode, &fp->inode->cache_tree);
                idx++;
        }
#endif
        if (evict_inode->close_node.prev != NULL && evict_inode->close_node.next!=NULL)
                list_del(&evict_inode->close_node);
        put_inode(evict_inode, NULL);

    }

}

int do_evict() {
        int retval = 0;
        if (stopeviction) return retval;

        debug_printf("do_evict, evict_closed_files\n");
        evict_closed_files();
        debug_printf("do_evict, evict_closed_files done\n");

        /* printf("do_evict, evict_interval_nodes, cache_size: %ld \n", cache_size); */
        evict_interval_nodes();
        /* printf("do_evict, evict_interval_nodes done, cache_size: %ld\n", cache_size); */

        return retval;
}
