#include "crfs_cache.h"

unsigned long hit_counter = 0;
unsigned long cache_size = 0;

unsigned long CACHE_LIMIT = 0;

__u64 cxl_user_addr;
__u64 cxl_mem_map[CXL_MEM_PG_NUM];

#ifdef GLOBAL_INTERVAL_TREE

int cache_insert(nvme_cmdrw_t *cmdrw, void* buf, unsigned long start, unsigned long end) {
        int ret = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;
        if (cmdrw == NULL) {
            ret = -1;
            return ret;
        }
        /* printf("cxl cache insert, start: %ld, end: %ld\n", start, end); */
        if ((cmdrw->buf_start == 0 && cmdrw->buf_end == 0) || cmdrw->blk_addr == NULL) {
            /* cxl_malloc returns a fix sized block from the cxl memory pool */
            cmdrw->blk_addr = (void*)vmalloc(NODE_SIZE_LIMIT);
            cmdrw->buf_start = start;
            cmdrw->buf_end = end;
            cache_size += NODE_SIZE_LIMIT;
        }

        node_buffer_offset = start - (cmdrw->buf_start / NODE_SIZE_LIMIT) * NODE_SIZE_LIMIT; 
        copy_size = end - start + 1;
        

        if (is_vmalloc_addr(cmdrw->blk_addr)) {
                memcpy((void *)cmdrw->blk_addr + node_buffer_offset,
                                (void *)buf, copy_size);
        }

        ret = copy_size;

        if (start < cmdrw->buf_start) {
            cmdrw->buf_start = start;
        }

        if (end > cmdrw->buf_end) {
            cmdrw->buf_end = end;
        }

        /* printk("cache insert, start: %ld, end: %ld, ret: %ld\n", start, end, ret); */
        return ret;
}

int cache_read(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw, nvme_cmdrw_t *cmdrw_mop, void* buf, unsigned long start, unsigned long end) {

        int ret = 0;
        int retval = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;
        int isappend = 0;
        char *p = NULL;

        if (cmdrw == NULL) {
                ret = -1;
                return ret;
        }

        /* printf("cxl cache insert, start: %ld, end: %ld\n", start, end); */
        if ((cmdrw->buf_start == 0 && cmdrw->buf_end == 0) || cmdrw->blk_addr == NULL) {
            /* cxl_malloc returns a fix sized block from the cxl memory pool */
            cmdrw->blk_addr = (void*)vmalloc(NODE_SIZE_LIMIT);
            cmdrw->buf_start = (start / NODE_SIZE_LIMIT) * NODE_SIZE_LIMIT;
            cmdrw->buf_end = cmdrw->buf_start  + NODE_SIZE_LIMIT -1;
            cache_size += NODE_SIZE_LIMIT;

            cmdrw_mop->common.opc = nvme_cmd_read;

            p = (__force char __user *)cmdrw->blk_addr;
            cmdrw_mop->blk_addr  = (uint64_t)p;
            cmdrw_mop->common.prp2 = (uint64_t)p;

            cmdrw_mop->slba = start;
            cmdrw_mop->nlb = end - start + 1;

            isappend = 0;

            /* read data block internally */
            retval = vfio_crfss_io_read(rd, cmdrw_mop, isappend);

        }

        /* printf("cxl cache read, start: %ld, end: %ld\n", start, end); */
        node_buffer_offset = start - (cmdrw->buf_start / NODE_SIZE_LIMIT) * NODE_SIZE_LIMIT;
        copy_size = end - start + 1;

        if (start >= cmdrw->buf_start && cmdrw->buf_end >= end)  {
                if (is_vmalloc_addr(cmdrw->blk_addr)) {
                        memcpy(buf, (void *)cmdrw->blk_addr + node_buffer_offset,
                                        copy_size);
                }
                ret += (end - start + 1);
        }

        /* printf("cxl read data, start: %ld, end: %ld, buf_s: %ld, buf_e: %ld, ret: %d\n", start, end, node->cxl_buf_start, node->cxl_buf_end, ret); */
        return ret;
}


int cache_rw(struct crfss_fstruct *rd, int cache_rw_op, nvme_cmdrw_t *cmdrw, nvme_cmdrw_t *cmdrw_mop, void* buf,
                unsigned long start, unsigned long end) {
        int retval = 0;
        if (buf == NULL) {
                printk("buffer is NULL\n");
                retval = -1;
                goto exit_node_rw;
        }
        switch (cache_rw_op) {
                case CACHE_WRITE_OP:
                case CACHE_APPEND_OP:
                        retval = cache_insert(cmdrw, buf, start, end);
                        break;
                case CACHE_READ_OP:
                        retval = cache_read(rd, cmdrw, cmdrw_mop, buf, start, end);
                        break;
                default:
                        printk("unsupport cache op\n");
                        retval = -1;
                        goto exit_node_rw;
        }
exit_node_rw:
        return retval;

}
int cache_evict(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw_after, 
        unsigned long slba, unsigned long nlb, struct rb_root *root) {
        return 0;
}
#else
int read_data_from_interval_node(struct req_tree_entry *node, void *buf,
                unsigned long start, unsigned long end) {
        
        int ret = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;

        if (node == NULL) {
                ret = -1;
                return ret;
        }
        if (node->size <= 0 || node->blk_addr == NULL) {
                ret = -1;
                return ret;
        }

        node_buffer_offset = start - node->it.start;
        copy_size = end - start + 1; 

        if (start >= node->buf_start && node->buf_end >= end)  {
                memcpy(buf, (void *)node->blk_addr + node_buffer_offset,
                                copy_size);
                ret += (end - start + 1);
        } 

        /* printk("read data, start: %ld, end: %ld, buf_s: %ld, buf_e: %ld, ret: %d\n", start, end, node->buf_start, node->buf_end, ret); */
        return ret;

}

int insert_cmd_to_interval_node(struct req_tree_entry *node, char *buf,
                                unsigned long start, unsigned long end) {
        int ret = 0;
        unsigned long copy_size = 0;
        unsigned long node_buffer_offset = 0;
        if (node == NULL) {
                ret = -1;
                return ret;
        }
        if (node->size <= 0 || node->blk_addr == NULL) {
                /* node->blk_addr = (void*)kmalloc(NODE_SIZE_LIMIT, GFP_KERNEL); */
                node->blk_addr = (void*)vmalloc(NODE_SIZE_LIMIT);
                node->buf_start = start;
                node->buf_end = end;
                node->size = NODE_SIZE_LIMIT;
                cache_size += NODE_SIZE_LIMIT;
        }
        
        node_buffer_offset = start - node->it.start;
        copy_size = end - start + 1; 

        memcpy((void *)node->blk_addr + node_buffer_offset,
                        (void *)buf, copy_size);

        if (start < node->buf_start) {
                node->buf_start = start;
        }

        if (end > node->buf_end) {
                node->buf_end = end;
        }

        /* printk("insert command, start: %ld, end: %ld, node start: %ld, node end: %ld, node_size: %ld\n",   */
                        /* start, end, node->it.start, node->it.last, node->size);  */

        return ret;
}

struct req_tree_entry *create_new_interval_node(void) {
        struct req_tree_entry *new_tree_node = NULL;
        new_tree_node = kmalloc(sizeof(struct req_tree_entry), GFP_KERNEL);
        /* new_tree_node = (struct req_tree_entry *)vmalloc(sizeof(struct req_tree_entry)); */
        if (!new_tree_node) {
                printk("Failed to allocate interval tree node\n");
                return NULL;
        }

        memset(new_tree_node, 0, sizeof(struct req_tree_entry));
        new_tree_node->size = 0;
        new_tree_node->it.start = 0;
        new_tree_node->it.last = 0;

        return new_tree_node;
}

int cache_evict(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw_after, 
        unsigned long slba, unsigned long nlb, struct rb_root *root) {

        int retval = 0;
        unsigned long start = slba;
        unsigned long end = slba + nlb - 1;
        struct req_tree_entry *tree_node = NULL;
        struct interval_tree_node *it;
        it = interval_tree_iter_first(root, start, end);

	while (it) {
		tree_node = container_of(it, struct req_tree_entry, it);
		if (!tree_node) {
                        printk("Get NULL object!\n");
                        retval = -1;
                        goto evict_cache_exit;
                }

                if (it->start == 0 && it->last ==0) {
                        break;
                }

                if (tree_node->blk_addr) {
                        cmdrw_after->common.prp2 = (u64)tree_node->blk_addr;
                        cmdrw_after->blk_addr = (u64)tree_node->blk_addr;
                        cmdrw_after->common.opc = nvme_cmd_write;
                        cmdrw_after->slba = slba;
                        cmdrw_after->nlb = nlb;

                        retval = vfio_crfss_io_write(rd, cmdrw_after);

                        vfree(tree_node->blk_addr);
                        tree_node->blk_addr = NULL;
                        tree_node->size = 0;
                        cache_size -= NODE_SIZE_LIMIT;
                }

                if (start >= it->start && it->last >= end) {
                        start = end = 0;
                        /* kfree(tree_node); */
                        break;
                } else if (start >= it->start && end > it->last)   {
                        start = it->last + 1;
                } else if (it->start > start && end <= it->last) {
                        end = it->start - 1;

                }

                it = interval_tree_iter_next(it, start, end);
	}
evict_cache_exit:
        return retval;
}

int interval_node_rw(int cache_rw_op,struct req_tree_entry *tree_node, void *buf, 
                unsigned long start, unsigned long end) {
        int retval = 0;
        if (buf == NULL) {
                printk("buffer is NULL\n");
                retval = -1;
                goto exit_node_rw;

        }
        switch (cache_rw_op) {
                case CACHE_WRITE_OP:
                        retval = insert_cmd_to_interval_node(tree_node, buf,
                                        start, end);
                        break;
                case CACHE_READ_OP:
                        retval = read_data_from_interval_node(tree_node, buf, 
                                        start, end);
                        break;  
                default:
                        printk("unsupport cache op\n");
                        retval = -1;
                        goto exit_node_rw;

        }
exit_node_rw:
        return retval;

}

int insert_new_tree_node(struct rb_root *root, struct tree_rw_req* tree_rw) {
        struct req_tree_entry *new_tree_node = NULL;
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
        /* printk("no overlap, insert new, start: %ld, end: %ld, start_idx: %d, end_idx: %d\n", start, end, start_idx, end_idx);  */
        for (i = start_idx; i <= end_idx; i++) {
                new_node_start = i * NODE_SIZE_LIMIT;
                new_node_end = new_node_start + NODE_SIZE_LIMIT - 1;
                new_tree_node = create_new_interval_node();
                /* new_tree_node->fp = fp; */
                new_tree_node->it.start = new_node_start;
                new_tree_node->it.last = new_node_end;

                /* printk("idx: %ld, new_node: %p, new_node_start: %ld, new_node_end: %ld\n", i, new_tree_node, new_node_start, new_node_end);  */
                interval_tree_insert(&new_tree_node->it, root);
                if (end > new_node_end) {
                        insert_cmd_to_interval_node(
                                        new_tree_node, buf + buf_off, start, new_node_end);
                        buf_off += (new_node_end - start + 1);
                        start = new_node_end + 1;
                } else {
                        insert_cmd_to_interval_node(
                                        new_tree_node, buf + buf_off, start, end);
                }

        }
}

int interval_tree_rw(struct rb_root *root, struct tree_rw_req* tree_rw, int cache_rw_op) {

        int retval = 0;
        int cache_idx = 0;
        struct interval_tree_node *it = NULL;
        struct req_tree_entry *tree_node = NULL;
        struct tree_rw_ret;
        void* buf = tree_rw->buf;
        unsigned long start = tree_rw->start;
        unsigned long end = tree_rw->end;
        unsigned long buf_off = 0;

        it = interval_tree_iter_first(root, start, end);

	    while (it) {
		        tree_node = container_of(it, struct req_tree_entry, it);
		        if (!tree_node) {
                        printk("Get NULL object!\n");
                        retval = -1;
                        goto exit_iterate_tree;
                }

                if (it->start == 0 && it->last ==0) {
                        break;
                }

                if (start >= it->start && it->last >= end) {
                        /* printk("hit1, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld\n", start, */
                                /* end, it->start, it->last);  */
                        retval += interval_node_rw(cache_rw_op, tree_node, 
                                        buf + buf_off, start, end);
                        start = end = 0;
                        break;
                } else if (start >= it->start && end > it->last)   {
                        /* printk("hit2, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld\n", start, */
                                /* end, it->start, it->last);  */
                        retval += interval_node_rw(cache_rw_op, tree_node, 
                                        buf + buf_off, start, it->last);

                        buf_off += (it->last - start + 1);
                        start = it->last + 1;
                } else if (it->start > start && end <= it->last) {
                        /* printk("hit3, insert, find a interval, start: %ld, end: %ld, interval start: %ld, end: %ld\n", start, */
                                /* end, it->start, it->last);  */
                        retval += interval_node_rw(cache_rw_op, tree_node, 
                                        buf + (it->start - start), it->start, end);

                        end = it->start - 1;

                }
                if ((start == 0 && end == 0) || start > end) {
                        break;
                }
                it = interval_tree_iter_next(it, start, end);
	}

exit_iterate_tree:
        tree_rw->start = start;
        tree_rw->end = end;
        tree_rw->buf_off = buf_off;
        return retval;
}

int cache_read(struct inode* inode, unsigned long slba, unsigned long nlb, char *buf,
               struct rb_root *root) {
        int retval = 0;
        struct tree_rw_req tree_rw;

        unsigned long start = (slba == DEVFS_INVALID_SLBA? 0: slba);
        unsigned long end = slba + nlb - 1;

        tree_rw.buf = buf;
        tree_rw.start = start;
        tree_rw.end = end;

        spin_lock(&inode->cache_tree_lock);
        /* printk("cache_read, slba: %ld, end: %ld\n", slba, end); */
        retval = interval_tree_rw(root, &tree_rw, CACHE_READ_OP);

        /*
         * if(retval < 0) {
         *         printk("interval tree read failed\n");
         * }
         */

err_write_submission_tree_search:
        /* printk("cache_read, slba: %ld, end: %ld, retval: %ld done\n", slba, end, retval); */
        spin_unlock(&inode->cache_tree_lock);
	return retval;
}

int cache_insert(struct inode *inode, void* blk_addr, unsigned long slba, unsigned long nlb,
                 struct rb_root *root) {
        int retval = 0;
        struct tree_rw_req tree_rw;

        unsigned long start = slba;
        unsigned long end = slba + nlb - 1;


        if (root == NULL) {
                printk("root is null\n");
        }

        tree_rw.buf = blk_addr;
        tree_rw.start = start;
        tree_rw.end = end;

        /* printk("do insert, start: %ld, end: %ld\n", start, end); */

        spin_lock(&inode->cache_tree_lock);

        interval_tree_rw(root, &tree_rw, CACHE_WRITE_OP);

	    if (tree_rw.start < tree_rw.end) {
                insert_new_tree_node(root, &tree_rw);
        }

err_submission_tree_insert:
        /* printk("cache insert done\n"); */
        spin_unlock(&inode->cache_tree_lock);
        return retval;
}
#endif
