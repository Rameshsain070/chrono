#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>

/* Wraps pthread mutex primitives behind a project-specific mutex type. */
typedef struct {
    pthread_mutex_t native_mutex;
} CMutex;

/* Implements a counting semaphore using pthread mutex and condition variable. */
typedef struct {
    pthread_mutex_t guard_mutex;
    pthread_cond_t count_condition;
    int count;
} CSemaphore;

/* Initializes a CMutex instance. */
int cmutex_init(CMutex *mutex_handle);

/* Locks a CMutex instance. */
int cmutex_lock(CMutex *mutex_handle);

/* Unlocks a CMutex instance. */
int cmutex_unlock(CMutex *mutex_handle);

/* Destroys a CMutex instance. */
int cmutex_destroy(CMutex *mutex_handle);

/* Initializes a CSemaphore with an initial count. */
int csem_init(CSemaphore *semaphore_handle, int initial_count);

/* Attempts to decrement a CSemaphore count and returns -1 with EAGAIN when unavailable. */
int sem_wait(CSemaphore *semaphore_handle);

/* Increments a CSemaphore count and wakes one waiting thread. */
int sem_signal(CSemaphore *semaphore_handle);

/* Returns the current count of a CSemaphore or -1 on error. */
int csem_get_count(CSemaphore *semaphore_handle);

/* Destroys a CSemaphore instance. */
int csem_destroy(CSemaphore *semaphore_handle);

#endif
