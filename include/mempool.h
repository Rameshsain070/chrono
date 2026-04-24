#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h>

#define MEMPOOL_TOTAL_BYTES 1024U
#define MEMPOOL_BLOCK_SIZE 32U
#define MEMPOOL_BLOCK_COUNT (MEMPOOL_TOTAL_BYTES / MEMPOOL_BLOCK_SIZE)

/* Allocates memory from the fixed block pool and returns NULL on failure. */
void *mem_alloc(size_t requested_size);

/* Frees a previously allocated memory region from the fixed block pool. */
void mem_free(void *memory_ptr);

/* Prints current allocator usage and fragmentation statistics. */
void mem_report(void);

/* Returns currently allocated bytes in the fixed block pool. */
size_t mem_used_bytes(void);

/* Returns total bytes in the fixed block pool. */
size_t mem_total_bytes(void);

#endif
