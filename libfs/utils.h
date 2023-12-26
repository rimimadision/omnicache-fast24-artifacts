#ifndef DEVFSLIB_H
#define DEVFSLIB_H

#include <stddef.h>
#include <pthread.h>
#include <stdlib.h>

//#include <mm.h>

/*
 * memory allocation utils
 */
#ifdef PARAFS_SHM
typedef struct open_file_table* open_file_table_ptr;
MM* shm;
#endif

void crfs_mm_init();
void *crfs_malloc(size_t size);
void crfs_free(void *ptr);

/*
 * locking utils
 */
#define CRFS_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER;
#define CRFS_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER;

typedef pthread_mutex_t crfs_mutex_t;
typedef pthread_rwlock_t crfs_rwlock_t;
typedef pthread_spinlock_t crfs_spinlock_t;

/* mutex lock */
int crfs_mutex_init(crfs_mutex_t *mutex);
int crfs_mutex_lock(crfs_mutex_t *mutex);
int crfs_mutex_trylock(crfs_mutex_t *mutex);
int crfs_mutex_unlock(crfs_mutex_t *mutex);

/* spin lock */
int crfs_spin_init(crfs_spinlock_t *lock, int pshared);
int crfs_spin_lock(crfs_spinlock_t *lock);
int crfs_spin_trylock(crfs_spinlock_t *lock);
int crfs_spin_unlock(crfs_spinlock_t *lock);

/* rw lock */
int crfs_rwlock_init(crfs_rwlock_t *rwlock); 

int crfs_rwlock_rdlock(crfs_rwlock_t *rwlock);

int crfs_rwlock_wrlock(crfs_rwlock_t *rwlock);

int crfs_rwlock_unlock(crfs_rwlock_t *rwlock);

/*
 * lru list ops.
 */

void add_to_lru_list(struct list_head* lru_node, struct list_head* lru_tail, crfs_mutex_t *mutex);
void remove_from_lru_list(struct list_head* lru_node, crfs_mutex_t *mutex);
void lru_list_update(struct list_head* lru_node, struct list_head* lru_tail, crfs_mutex_t *mutex);
#endif
