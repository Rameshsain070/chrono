#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "task.h"

#define TIME_SLICE_MS 100
#define SCHEDULER_MAX_TASKS 32

/* Aggregates scheduler runtime metrics. */
typedef struct {
    uint64_t total_context_switches;
    size_t total_tasks;
} SchedulerStats;

/* Initializes scheduler data structures and resets runtime state. */
int scheduler_init(void);

/* Adds a task to the scheduler and returns 0 on success. */
int scheduler_add_task(TCB *task_control_block);

/* Runs the scheduler loop until stop is requested or all tasks are dead. */
void scheduler_run(void);

/* Prints scheduler statistics and task runtime details. */
void scheduler_stats(void);

/* Requests scheduler termination at the next scheduling boundary. */
void scheduler_request_stop(void);

/* Returns a snapshot of scheduler runtime metrics. */
SchedulerStats scheduler_get_stats(void);

/* Returns number of tasks currently registered in scheduler. */
size_t scheduler_get_task_count(void);

/* Returns task pointer at index or NULL when index is invalid. */
TCB *scheduler_get_task_by_index(size_t task_index);

/* Converts a TaskState enum value to a printable string. */
const char *scheduler_task_state_string(TaskState task_state);

#endif
