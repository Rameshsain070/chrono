#include "scheduler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Stores task pointers registered with the scheduler. */
static TCB *g_tasks[SCHEDULER_MAX_TASKS];

/* Stores per-task round-robin sequence values for fair equal-priority scheduling. */
static uint64_t g_rr_sequence[SCHEDULER_MAX_TASKS];

/* Stores current number of registered tasks. */
static size_t g_task_count = 0U;

/* Tracks total context switches performed by scheduler. */
static uint64_t g_context_switches = 0U;

/* Tracks latest dispatched task index to detect context switches. */
static int g_last_task_index = -1;

/* Tracks if scheduler loop should terminate. */
static int g_stop_requested = 0;

/* Stores global sequence counter used for round-robin ordering. */
static uint64_t g_sequence_counter = 0U;

/* Represents a task candidate in the scheduling heap. */
typedef struct {
    size_t task_index;
    int priority;
    uint64_t rr_order;
} HeapEntry;

/* Stores heap entries for current scheduling cycle. */
static HeapEntry g_heap[SCHEDULER_MAX_TASKS];

/* Stores active heap size in current scheduling cycle. */
static size_t g_heap_size = 0U;

/* Converts task state to a user-readable name. */
const char *scheduler_task_state_string(TaskState task_state) {
    switch (task_state) {
        case READY:
            return "READY";
        case RUNNING:
            return "RUNNING";
        case BLOCKED:
            return "BLOCKED";
        case SUSPENDED:
            return "SUSPENDED";
        case DEAD:
            return "DEAD";
        default:
            return "UNKNOWN";
    }
}

/* Compares two heap entries by priority and round-robin order. */
static int scheduler_heap_less(const HeapEntry *left_entry, const HeapEntry *right_entry) {
    if (left_entry->priority != right_entry->priority) {
        return left_entry->priority < right_entry->priority;
    }
    return left_entry->rr_order < right_entry->rr_order;
}

/* Swaps two heap entries in place. */
static void scheduler_heap_swap(HeapEntry *first_entry, HeapEntry *second_entry) {
    HeapEntry temporary_entry = *first_entry;
    *first_entry = *second_entry;
    *second_entry = temporary_entry;
}

/* Restores heap ordering from a node upward. */
static void scheduler_heap_sift_up(size_t start_index) {
    size_t current_index = start_index;

    while (current_index > 0U) {
        size_t parent_index = (current_index - 1U) / 2U;
        if (!scheduler_heap_less(&g_heap[current_index], &g_heap[parent_index])) {
            break;
        }
        scheduler_heap_swap(&g_heap[current_index], &g_heap[parent_index]);
        current_index = parent_index;
    }
}

/* Restores heap ordering from a node downward. */
static void scheduler_heap_sift_down(size_t start_index) {
    size_t current_index = start_index;

    while (1) {
        size_t left_child = (2U * current_index) + 1U;
        size_t right_child = left_child + 1U;
        size_t smallest_index = current_index;

        if (left_child < g_heap_size && scheduler_heap_less(&g_heap[left_child], &g_heap[smallest_index])) {
            smallest_index = left_child;
        }

        if (right_child < g_heap_size && scheduler_heap_less(&g_heap[right_child], &g_heap[smallest_index])) {
            smallest_index = right_child;
        }

        if (smallest_index == current_index) {
            break;
        }

        scheduler_heap_swap(&g_heap[current_index], &g_heap[smallest_index]);
        current_index = smallest_index;
    }
}

/* Pushes a task candidate into the min-heap. */
static int scheduler_heap_push(size_t task_index) {
    TCB *task_control_block = NULL;

    if (g_heap_size >= SCHEDULER_MAX_TASKS) {
        return -1;
    }

    task_control_block = g_tasks[task_index];
    if (task_control_block == NULL) {
        return -1;
    }

    g_heap[g_heap_size].task_index = task_index;
    g_heap[g_heap_size].priority = task_control_block->priority;
    g_heap[g_heap_size].rr_order = g_rr_sequence[task_index];
    scheduler_heap_sift_up(g_heap_size);
    ++g_heap_size;

    return 0;
}

/* Pops the next task candidate from the min-heap. */
static int scheduler_heap_pop(size_t *task_index_out) {
    if (task_index_out == NULL || g_heap_size == 0U) {
        return -1;
    }

    *task_index_out = g_heap[0].task_index;
    --g_heap_size;
    if (g_heap_size > 0U) {
        g_heap[0] = g_heap[g_heap_size];
        scheduler_heap_sift_down(0U);
    }

    return 0;
}

/* Returns non-zero when all registered tasks have reached DEAD state. */
static int scheduler_all_tasks_dead(void) {
    size_t task_index = 0U;

    for (task_index = 0U; task_index < g_task_count; ++task_index) {
        TCB *task_control_block = g_tasks[task_index];
        if (task_control_block != NULL && task_control_block->state != DEAD) {
            return 0;
        }
    }

    return 1;
}

/* Builds the scheduling heap from tasks that are eligible to run. */
static void scheduler_build_heap(void) {
    size_t task_index = 0U;
    int has_ready_tasks = 0;
    g_heap_size = 0U;

    for (task_index = 0U; task_index < g_task_count; ++task_index) {
        TCB *task_control_block = g_tasks[task_index];
        if (task_control_block != NULL && task_control_block->state == READY) {
            has_ready_tasks = 1;
            break;
        }
    }

    for (task_index = 0U; task_index < g_task_count; ++task_index) {
        TCB *task_control_block = g_tasks[task_index];
        if (task_control_block == NULL) {
            continue;
        }

        if (task_control_block->state == READY ||
            (!has_ready_tasks && task_control_block->state == BLOCKED)) {
            (void)scheduler_heap_push(task_index);
        }
    }
}

/* Computes elapsed milliseconds between two monotonic timestamps. */
static uint64_t scheduler_elapsed_ms(const struct timespec *start_time, const struct timespec *end_time) {
    uint64_t start_ms = 0U;
    uint64_t end_ms = 0U;

    if (start_time == NULL || end_time == NULL) {
        return 0U;
    }

    start_ms = ((uint64_t)start_time->tv_sec * 1000U) + ((uint64_t)start_time->tv_nsec / 1000000U);
    end_ms = ((uint64_t)end_time->tv_sec * 1000U) + ((uint64_t)end_time->tv_nsec / 1000000U);
    return (end_ms >= start_ms) ? (end_ms - start_ms) : 0U;
}

/* Sleeps for one scheduler time slice using nanosleep. */
static void scheduler_sleep_slice(void) {
    struct timespec sleep_interval;
    sleep_interval.tv_sec = TIME_SLICE_MS / 1000;
    sleep_interval.tv_nsec = (long)(TIME_SLICE_MS % 1000) * 1000000L;
    (void)nanosleep(&sleep_interval, NULL);
}

/* Initializes scheduler runtime state and clears prior task registrations. */
int scheduler_init(void) {
    (void)memset(g_tasks, 0, sizeof(g_tasks));
    (void)memset(g_rr_sequence, 0, sizeof(g_rr_sequence));
    g_task_count = 0U;
    g_context_switches = 0U;
    g_last_task_index = -1;
    g_stop_requested = 0;
    g_sequence_counter = 0U;
    g_heap_size = 0U;
    return 0;
}

/* Registers a task control block with scheduler management. */
int scheduler_add_task(TCB *task_control_block) {
    if (task_control_block == NULL || task_control_block->func == NULL) {
        return -1;
    }

    if (task_control_block->priority < 0 || g_task_count >= SCHEDULER_MAX_TASKS) {
        return -1;
    }

    if (task_control_block->state == DEAD) {
        task_control_block->state = READY;
    }

    g_tasks[g_task_count] = task_control_block;
    g_rr_sequence[g_task_count] = g_sequence_counter++;
    ++g_task_count;
    return 0;
}

/* Requests scheduler loop termination. */
void scheduler_request_stop(void) {
    g_stop_requested = 1;
}

/* Executes the scheduler dispatch loop until stop conditions are reached. */
void scheduler_run(void) {
    while (!g_stop_requested) {
        size_t selected_task_index = 0U;
        TCB *selected_task = NULL;
        struct timespec start_time;
        struct timespec end_time;

        if (scheduler_all_tasks_dead()) {
            break;
        }

        scheduler_build_heap();
        if (scheduler_heap_pop(&selected_task_index) != 0) {
            scheduler_sleep_slice();
            continue;
        }

        selected_task = g_tasks[selected_task_index];
        if (selected_task == NULL || selected_task->state == DEAD || selected_task->state == SUSPENDED) {
            scheduler_sleep_slice();
            continue;
        }

        if (g_last_task_index != (int)selected_task_index) {
            ++g_context_switches;
            g_last_task_index = (int)selected_task_index;
        }

        selected_task->state = RUNNING;
        if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
            start_time.tv_sec = 0;
            start_time.tv_nsec = 0;
        }

        selected_task->func(selected_task->arg);

        if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
            end_time.tv_sec = start_time.tv_sec;
            end_time.tv_nsec = start_time.tv_nsec;
        }

        selected_task->cpu_time_ms += scheduler_elapsed_ms(&start_time, &end_time);

        if (selected_task->state == RUNNING) {
            selected_task->state = READY;
        }

        g_rr_sequence[selected_task_index] = g_sequence_counter++;
        scheduler_sleep_slice();
    }
}

/* Returns scheduler metrics for external reporting. */
SchedulerStats scheduler_get_stats(void) {
    SchedulerStats scheduler_stats_snapshot;
    scheduler_stats_snapshot.total_context_switches = g_context_switches;
    scheduler_stats_snapshot.total_tasks = g_task_count;
    return scheduler_stats_snapshot;
}

/* Returns number of scheduler-managed tasks. */
size_t scheduler_get_task_count(void) {
    return g_task_count;
}

/* Returns scheduler task pointer for a given index. */
TCB *scheduler_get_task_by_index(size_t task_index) {
    if (task_index >= g_task_count) {
        return NULL;
    }
    return g_tasks[task_index];
}

/* Prints scheduler metrics and current task states to stdout. */
void scheduler_stats(void) {
    size_t task_index = 0U;
    SchedulerStats snapshot = scheduler_get_stats();

    (void)printf("[scheduler] Context switches: %llu | Task count: %zu\n",
                 (unsigned long long)snapshot.total_context_switches,
                 snapshot.total_tasks);

    for (task_index = 0U; task_index < g_task_count; ++task_index) {
        TCB *task_control_block = g_tasks[task_index];
        if (task_control_block == NULL) {
            continue;
        }

        (void)printf("  Task %d | Priority %d | State %s | CPU %llums\n",
                     task_control_block->task_id,
                     task_control_block->priority,
                     scheduler_task_state_string(task_control_block->state),
                     (unsigned long long)task_control_block->cpu_time_ms);
    }
}
