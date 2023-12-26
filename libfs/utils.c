#include "list.h"
#include "utils.h"

//#include "spinlock-xchg.h"
//#include "spinlock-ticket.h"
#include "spinlock-xchg-backoff.h"

//#define _ENABLE_PTHREAD_LOCK

/*
 * memory allocation implementation
 */

void crfs_mm_init() {
#ifdef PARAFS_SHM
        shm = mm_create(SHM_SIZE, SHM_POOL);
        if (!shm) {
                printf("failed to allocate shared memory!\n");
                exit(1);
        }

        ufile_table_ptr = malloc(sizeof(struct open_file_table));
#endif
}
void *crfs_malloc(size_t size) {
#ifdef PARAFS_SHM
		return mm_malloc(shm, size);
#else
		return malloc(size);
#endif
}

void crfs_free(void *ptr) {
#ifdef PARAFS_SHM
		mm_free(shm, ptr);
#else
		free(ptr);
#endif
}

/*
 * locking implementation
 */

/* mutex lock */
int crfs_mutex_init(crfs_mutex_t *mutex) {
#ifdef _ENABLE_PTHREAD_LOCK
        return pthread_mutex_init(mutex, NULL);
#else
#endif
}

int crfs_mutex_lock(crfs_mutex_t *mutex) {
#ifdef _ENABLE_PTHREAD_LOCK
        return pthread_mutex_lock(mutex);
#else
	spin_lock(mutex);
	return 0;
#endif
}

int crfs_mutex_trylock(crfs_mutex_t *mutex) {
#ifdef _ENABLE_PTHREAD_LOCK
        return pthread_mutex_trylock(mutex);
#else
	spin_lock(mutex);
	return 0;
#endif
}

int crfs_mutex_unlock(crfs_mutex_t *mutex) {
#ifdef _ENABLE_PTHREAD_LOCK
        return pthread_mutex_unlock(mutex);
#else
	spin_unlock(mutex);
	return 0;
#endif
}




/* spin lock */
int crfs_spin_init(crfs_spinlock_t *lock, int pshared) {
        return pthread_spin_init(lock, pshared);
}

int crfs_spin_lock(crfs_spinlock_t *lock) {
        return pthread_spin_lock(lock);
}

int crfs_spin_trylock(crfs_spinlock_t *lock) {
        return pthread_spin_trylock(lock);
}

int crfs_spin_unlock(crfs_spinlock_t *lock) {
        return pthread_spin_unlock(lock);
}

/* rw lock */
int crfs_rwlock_init(crfs_rwlock_t *rwlock) {
	return pthread_rwlock_init(rwlock, NULL);
}

int crfs_rwlock_rdlock(crfs_rwlock_t *rwlock) {
	return pthread_rwlock_rdlock(rwlock);
}

int crfs_rwlock_wrlock(crfs_rwlock_t *rwlock) {
	return pthread_rwlock_wrlock(rwlock);
}

int crfs_rwlock_unlock(crfs_rwlock_t *rwlock) {
	return pthread_rwlock_unlock(rwlock);
}

void add_to_lru_list(struct list_head* lru_node, struct list_head* lru_tail, crfs_mutex_t *mutex) {
        crfs_mutex_lock(mutex);
        list_add_tail(lru_node, lru_tail);
        crfs_mutex_unlock(mutex);
}


void remove_from_lru_list(struct list_head* lru_node, crfs_mutex_t *mutex){
        crfs_mutex_lock(mutex);
        if (lru_node->prev != NULL && lru_node->next!=NULL)
            list_del(lru_node);
        crfs_mutex_unlock(mutex);
}

void lru_list_update(struct list_head* lru_node, struct list_head* lru_tail, crfs_mutex_t *mutex) {
#if 0
        crfs_mutex_lock(mutex);
        list_move_tail(lru_node, lru_tail);
        crfs_mutex_unlock(mutex);
#endif
}


