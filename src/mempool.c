#include "mempool.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

/* Stores contiguous fixed-size blocks used by the allocator. */
static uint8_t g_pool_bytes[MEMPOOL_TOTAL_BYTES];

/* Marks whether each block is currently allocated. */
static uint8_t g_block_in_use[MEMPOOL_BLOCK_COUNT];

/* Stores allocation run length at run start and -1 for continuation blocks. */
static int16_t g_allocation_map[MEMPOOL_BLOCK_COUNT];

/* Guards memory pool metadata for thread-safe access. */
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Calculates the number of blocks required to satisfy requested bytes. */
static size_t mem_required_blocks(size_t requested_size) {
    if (requested_size == 0U) {
        return 0U;
    }
    return (requested_size + MEMPOOL_BLOCK_SIZE - 1U) / MEMPOOL_BLOCK_SIZE;
}

/* Finds the first contiguous free run that can hold required blocks. */
static int mem_find_free_run(size_t required_blocks) {
    size_t current_run = 0U;
    size_t run_start = 0U;
    size_t block_index = 0U;

    for (block_index = 0U; block_index < MEMPOOL_BLOCK_COUNT; ++block_index) {
        if (g_block_in_use[block_index] == 0U) {
            if (current_run == 0U) {
                run_start = block_index;
            }
            ++current_run;
            if (current_run >= required_blocks) {
                return (int)run_start;
            }
        } else {
            current_run = 0U;
        }
    }

    return -1;
}

/* Allocates memory from the fixed-size block pool. */
void *mem_alloc(size_t requested_size) {
    size_t blocks_needed = 0U;
    int run_start = -1;
    size_t block_offset = 0U;

    if (requested_size == 0U || requested_size > MEMPOOL_TOTAL_BYTES) {
        return NULL;
    }

    blocks_needed = mem_required_blocks(requested_size);
    if (blocks_needed == 0U || blocks_needed > MEMPOOL_BLOCK_COUNT) {
        return NULL;
    }

    if (pthread_mutex_lock(&g_pool_mutex) != 0) {
        return NULL;
    }

    run_start = mem_find_free_run(blocks_needed);
    if (run_start < 0) {
        (void)pthread_mutex_unlock(&g_pool_mutex);
        return NULL;
    }

    for (block_offset = 0U; block_offset < blocks_needed; ++block_offset) {
        size_t block_index = (size_t)run_start + block_offset;
        g_block_in_use[block_index] = 1U;
        if (block_offset == 0U) {
            g_allocation_map[block_index] = (int16_t)blocks_needed;
        } else {
            g_allocation_map[block_index] = -1;
        }
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);
    return (void *)&g_pool_bytes[(size_t)run_start * MEMPOOL_BLOCK_SIZE];
}

/* Frees memory back to the fixed-size block pool. */
void mem_free(void *memory_ptr) {
    uintptr_t pool_start = (uintptr_t)&g_pool_bytes[0];
    uintptr_t pool_end = (uintptr_t)&g_pool_bytes[MEMPOOL_TOTAL_BYTES - 1U] + 1U;
    uintptr_t pointer_value = (uintptr_t)memory_ptr;
    size_t block_index = 0U;
    int16_t allocation_blocks = 0;
    size_t block_offset = 0U;

    if (memory_ptr == NULL) {
        return;
    }

    if (pointer_value < pool_start || pointer_value >= pool_end) {
        return;
    }

    if (((pointer_value - pool_start) % MEMPOOL_BLOCK_SIZE) != 0U) {
        return;
    }

    if (pthread_mutex_lock(&g_pool_mutex) != 0) {
        return;
    }

    block_index = (size_t)((pointer_value - pool_start) / MEMPOOL_BLOCK_SIZE);
    allocation_blocks = g_allocation_map[block_index];
    if (allocation_blocks <= 0) {
        (void)pthread_mutex_unlock(&g_pool_mutex);
        return;
    }

    for (block_offset = 0U; block_offset < (size_t)allocation_blocks; ++block_offset) {
        size_t free_index = block_index + block_offset;
        if (free_index >= MEMPOOL_BLOCK_COUNT) {
            break;
        }
        g_block_in_use[free_index] = 0U;
        g_allocation_map[free_index] = 0;
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);
}

/* Computes bytes currently in use by active allocations. */
size_t mem_used_bytes(void) {
    size_t used_blocks = 0U;
    size_t block_index = 0U;

    if (pthread_mutex_lock(&g_pool_mutex) != 0) {
        return 0U;
    }

    for (block_index = 0U; block_index < MEMPOOL_BLOCK_COUNT; ++block_index) {
        if (g_block_in_use[block_index] != 0U) {
            ++used_blocks;
        }
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);
    return used_blocks * MEMPOOL_BLOCK_SIZE;
}

/* Returns total allocator capacity in bytes. */
size_t mem_total_bytes(void) {
    return MEMPOOL_TOTAL_BYTES;
}

/* Prints memory usage and fragmentation statistics for the pool. */
void mem_report(void) {
    size_t used_blocks = 0U;
    size_t free_blocks = 0U;
    size_t free_runs = 0U;
    size_t current_run = 0U;
    size_t largest_free_run = 0U;
    size_t block_index = 0U;
    double fragmentation_percent = 0.0;

    if (pthread_mutex_lock(&g_pool_mutex) != 0) {
        (void)printf("[mempool] Failed to lock allocator state for reporting.\n");
        return;
    }

    for (block_index = 0U; block_index < MEMPOOL_BLOCK_COUNT; ++block_index) {
        if (g_block_in_use[block_index] != 0U) {
            ++used_blocks;
            if (current_run > 0U) {
                ++free_runs;
                if (current_run > largest_free_run) {
                    largest_free_run = current_run;
                }
                current_run = 0U;
            }
        } else {
            ++free_blocks;
            ++current_run;
        }
    }

    if (current_run > 0U) {
        ++free_runs;
        if (current_run > largest_free_run) {
            largest_free_run = current_run;
        }
    }

    if (free_blocks > 0U) {
        fragmentation_percent = ((double)(free_blocks - largest_free_run) / (double)free_blocks) * 100.0;
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);

    (void)printf("[mempool] Used: %zu/%u bytes | Free runs: %zu | Fragmentation: %.2f%%\n",
                 used_blocks * MEMPOOL_BLOCK_SIZE,
                 MEMPOOL_TOTAL_BYTES,
                 free_runs,
                 fragmentation_percent);
}
