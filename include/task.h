#ifndef TASK_H
#define TASK_H

#include <stdint.h>

/* Represents all supported lifecycle states for a scheduled task. */
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    SUSPENDED,
    DEAD
} TaskState;

/* Stores task metadata used by the scheduler. */
typedef struct {
    int task_id;
    int priority;
    TaskState state;
    void (*func)(void *);
    void *arg;
    uint64_t cpu_time_ms;
} TCB;

#endif
