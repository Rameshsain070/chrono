#include "sync.h"

#include <errno.h>

/* Initializes a custom mutex wrapper around pthread mutex. */
int cmutex_init(CMutex *mutex_handle) {
    if (mutex_handle == NULL) {
        return -1;
    }
    return pthread_mutex_init(&mutex_handle->native_mutex, NULL);
}

/* Locks a custom mutex wrapper. */
int cmutex_lock(CMutex *mutex_handle) {
    if (mutex_handle == NULL) {
        return -1;
    }
    return pthread_mutex_lock(&mutex_handle->native_mutex);
}

/* Unlocks a custom mutex wrapper. */
int cmutex_unlock(CMutex *mutex_handle) {
    if (mutex_handle == NULL) {
        return -1;
    }
    return pthread_mutex_unlock(&mutex_handle->native_mutex);
}

/* Destroys a custom mutex wrapper. */
int cmutex_destroy(CMutex *mutex_handle) {
    if (mutex_handle == NULL) {
        return -1;
    }
    return pthread_mutex_destroy(&mutex_handle->native_mutex);
}

/* Initializes a counting semaphore implemented with mutex and condition variable. */
int csem_init(CSemaphore *semaphore_handle, int initial_count) {
    if (semaphore_handle == NULL || initial_count < 0) {
        return -1;
    }

    if (pthread_mutex_init(&semaphore_handle->guard_mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&semaphore_handle->count_condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&semaphore_handle->guard_mutex);
        return -1;
    }

    semaphore_handle->count = initial_count;
    return 0;
}

/* Attempts a non-blocking semaphore decrement and returns -1 if count is zero. */
int sem_wait(CSemaphore *semaphore_handle) {
    int return_code = 0;

    if (semaphore_handle == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    if (semaphore_handle->count <= 0) {
        errno = EAGAIN;
        return_code = -1;
    } else {
        --semaphore_handle->count;
    }

    if (pthread_mutex_unlock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    return return_code;
}

/* Increments semaphore count and signals one potential waiter. */
int sem_signal(CSemaphore *semaphore_handle) {
    if (semaphore_handle == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    ++semaphore_handle->count;
    if (pthread_cond_signal(&semaphore_handle->count_condition) != 0) {
        (void)pthread_mutex_unlock(&semaphore_handle->guard_mutex);
        return -1;
    }

    if (pthread_mutex_unlock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    return 0;
}

/* Reads the current semaphore count in a thread-safe manner. */
int csem_get_count(CSemaphore *semaphore_handle) {
    int current_count = -1;

    if (semaphore_handle == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    current_count = semaphore_handle->count;

    if (pthread_mutex_unlock(&semaphore_handle->guard_mutex) != 0) {
        return -1;
    }

    return current_count;
}

/* Destroys a counting semaphore and its internal synchronization resources. */
int csem_destroy(CSemaphore *semaphore_handle) {
    int mutex_result = 0;
    int condition_result = 0;

    if (semaphore_handle == NULL) {
        return -1;
    }

    mutex_result = pthread_mutex_destroy(&semaphore_handle->guard_mutex);
    condition_result = pthread_cond_destroy(&semaphore_handle->count_condition);
    if (mutex_result != 0 || condition_result != 0) {
        return -1;
    }

    return 0;
}
