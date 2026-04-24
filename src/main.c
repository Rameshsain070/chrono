#include <stdio.h>
#include <string.h>

#include "mempool.h"
#include "scheduler.h"
#include "sync.h"
#include "task.h"

#define BUFFER_CAPACITY 8
#define DASHBOARD_INTERVAL_TICKS 10
#define DEMO_RUNTIME_SECONDS 15

typedef struct {
    int value;
} BufferItem;

typedef struct {
    void *slots[BUFFER_CAPACITY];
    int head_index;
    int tail_index;
    int item_count;
    CMutex buffer_mutex;
    CSemaphore items_available;
    CSemaphore slots_available;
    int produced_total;
    int consumed_total;
} SharedBuffer;

typedef struct {
    TCB *self_task;
    SharedBuffer *shared_buffer;
    int next_value;
} ProducerTaskContext;

typedef struct {
    TCB *self_task;
    SharedBuffer *shared_buffer;
    int last_value;
} ConsumerTaskContext;

typedef struct {
    TCB *self_task;
    SharedBuffer *shared_buffer;
    unsigned int tick_counter;
} LoggerTaskContext;

typedef struct {
    TCB *self_task;
    SharedBuffer *shared_buffer;
    unsigned int tick_counter;
    unsigned int seconds_remaining;
} MonitorTaskContext;

typedef struct {
    int task_id;
    const char *task_name;
} TaskNameEntry;

/* Stores static task name mapping used by dashboard renderer. */
static TaskNameEntry g_task_names[] = {
    {1, "producer"},
    {2, "consumer"},
    {3, "logger"},
    {4, "monitor"}
};

/* Returns task name string for a given task id. */
static const char *find_task_name(int task_id) {
    size_t name_index = 0U;

    for (name_index = 0U; name_index < (sizeof(g_task_names) / sizeof(g_task_names[0])); ++name_index) {
        if (g_task_names[name_index].task_id == task_id) {
            return g_task_names[name_index].task_name;
        }
    }

    return "unknown";
}

/* Prints one dashboard frame with task and scheduler statistics. */
static void print_dashboard(const SharedBuffer *shared_buffer) {
    size_t task_count = scheduler_get_task_count();
    size_t task_index = 0U;
    SchedulerStats stats_snapshot = scheduler_get_stats();

    (void)printf("\n=== Chrono dashboard ===\n");
    (void)printf("%-7s %-10s %-9s %-11s %-12s\n", "task ID", "name", "priority", "state", "cpu_time_ms");

    for (task_index = 0U; task_index < task_count; ++task_index) {
        TCB *task_control_block = scheduler_get_task_by_index(task_index);
        if (task_control_block == NULL) {
            continue;
        }

        (void)printf("%-7d %-10s %-9d %-11s %-12llu\n",
                     task_control_block->task_id,
                     find_task_name(task_control_block->task_id),
                     task_control_block->priority,
                     scheduler_task_state_string(task_control_block->state),
                     (unsigned long long)task_control_block->cpu_time_ms);
    }

    (void)printf("Context switches: %llu\n", (unsigned long long)stats_snapshot.total_context_switches);
    (void)printf("Memory used/total: %zu/%zu bytes\n", mem_used_bytes(), mem_total_bytes());
    (void)printf("Buffer items: %d | Produced: %d | Consumed: %d\n",
                 shared_buffer != NULL ? shared_buffer->item_count : 0,
                 shared_buffer != NULL ? shared_buffer->produced_total : 0,
                 shared_buffer != NULL ? shared_buffer->consumed_total : 0);
    (void)printf("========================\n");
}

/* Runs one producer step that allocates and enqueues a buffer item. */
static void producer_task(void *task_argument) {
    ProducerTaskContext *context = (ProducerTaskContext *)task_argument;
    BufferItem *new_item = NULL;

    if (context == NULL || context->self_task == NULL || context->shared_buffer == NULL) {
        return;
    }

    if (sem_wait(&context->shared_buffer->slots_available) != 0) {
        context->self_task->state = READY;
        return;
    }

    new_item = (BufferItem *)mem_alloc(sizeof(BufferItem));
    if (new_item == NULL) {
        (void)sem_signal(&context->shared_buffer->slots_available);
        context->self_task->state = READY;
        return;
    }

    new_item->value = context->next_value++;

    if (cmutex_lock(&context->shared_buffer->buffer_mutex) != 0) {
        mem_free(new_item);
        (void)sem_signal(&context->shared_buffer->slots_available);
        context->self_task->state = READY;
        return;
    }

    if (context->shared_buffer->item_count >= BUFFER_CAPACITY) {
        (void)cmutex_unlock(&context->shared_buffer->buffer_mutex);
        mem_free(new_item);
        (void)sem_signal(&context->shared_buffer->slots_available);
        context->self_task->state = READY;
        return;
    }

    context->shared_buffer->slots[context->shared_buffer->tail_index] = new_item;
    context->shared_buffer->tail_index = (context->shared_buffer->tail_index + 1) % BUFFER_CAPACITY;
    context->shared_buffer->item_count += 1;
    context->shared_buffer->produced_total += 1;

    if (cmutex_unlock(&context->shared_buffer->buffer_mutex) != 0) {
        context->self_task->state = READY;
        return;
    }

    (void)sem_signal(&context->shared_buffer->items_available);
    context->self_task->state = READY;
}

/* Runs one consumer step that dequeues and frees a buffer item. */
static void consumer_task(void *task_argument) {
    ConsumerTaskContext *context = (ConsumerTaskContext *)task_argument;
    BufferItem *consumed_item = NULL;

    if (context == NULL || context->self_task == NULL || context->shared_buffer == NULL) {
        return;
    }

    if (sem_wait(&context->shared_buffer->items_available) != 0) {
        context->self_task->state = READY;
        return;
    }

    if (cmutex_lock(&context->shared_buffer->buffer_mutex) != 0) {
        (void)sem_signal(&context->shared_buffer->items_available);
        context->self_task->state = READY;
        return;
    }

    if (context->shared_buffer->item_count <= 0) {
        (void)cmutex_unlock(&context->shared_buffer->buffer_mutex);
        (void)sem_signal(&context->shared_buffer->items_available);
        context->self_task->state = READY;
        return;
    }

    consumed_item = (BufferItem *)context->shared_buffer->slots[context->shared_buffer->head_index];
    context->shared_buffer->slots[context->shared_buffer->head_index] = NULL;
    context->shared_buffer->head_index = (context->shared_buffer->head_index + 1) % BUFFER_CAPACITY;
    context->shared_buffer->item_count -= 1;
    context->shared_buffer->consumed_total += 1;

    if (cmutex_unlock(&context->shared_buffer->buffer_mutex) != 0) {
        context->self_task->state = READY;
        return;
    }

    (void)sem_signal(&context->shared_buffer->slots_available);

    if (consumed_item != NULL) {
        context->last_value = consumed_item->value;
        mem_free(consumed_item);
    }

    context->self_task->state = READY;
}

/* Runs one logger step that periodically emits summarized producer-consumer data. */
static void logger_task(void *task_argument) {
    LoggerTaskContext *context = (LoggerTaskContext *)task_argument;

    if (context == NULL || context->self_task == NULL || context->shared_buffer == NULL) {
        return;
    }

    context->tick_counter += 1U;
    if ((context->tick_counter % 5U) == 0U) {
        (void)printf("[logger] produced=%d consumed=%d buffered=%d\n",
                     context->shared_buffer->produced_total,
                     context->shared_buffer->consumed_total,
                     context->shared_buffer->item_count);
    }

    context->self_task->state = READY;
}

/* Runs one monitor step that prints dashboard output once per second and stops demo when done. */
static void monitor_task(void *task_argument) {
    MonitorTaskContext *context = (MonitorTaskContext *)task_argument;

    if (context == NULL || context->self_task == NULL || context->shared_buffer == NULL) {
        return;
    }

    context->tick_counter += 1U;
    if ((context->tick_counter % DASHBOARD_INTERVAL_TICKS) == 0U) {
        print_dashboard(context->shared_buffer);
        if (context->seconds_remaining > 0U) {
            context->seconds_remaining -= 1U;
        }
        if (context->seconds_remaining == 0U) {
            context->self_task->state = DEAD;
            scheduler_request_stop();
            return;
        }
    }

    context->self_task->state = READY;
}

/* Initializes shared producer-consumer resources and validates setup success. */
static int initialize_shared_buffer(SharedBuffer *shared_buffer) {
    if (shared_buffer == NULL) {
        return -1;
    }

    (void)memset(shared_buffer, 0, sizeof(*shared_buffer));

    if (cmutex_init(&shared_buffer->buffer_mutex) != 0) {
        return -1;
    }

    if (csem_init(&shared_buffer->items_available, 0) != 0) {
        (void)cmutex_destroy(&shared_buffer->buffer_mutex);
        return -1;
    }

    if (csem_init(&shared_buffer->slots_available, BUFFER_CAPACITY) != 0) {
        (void)csem_destroy(&shared_buffer->items_available);
        (void)cmutex_destroy(&shared_buffer->buffer_mutex);
        return -1;
    }

    return 0;
}

/* Releases shared producer-consumer resources. */
static void destroy_shared_buffer(SharedBuffer *shared_buffer) {
    if (shared_buffer == NULL) {
        return;
    }

    (void)csem_destroy(&shared_buffer->slots_available);
    (void)csem_destroy(&shared_buffer->items_available);
    (void)cmutex_destroy(&shared_buffer->buffer_mutex);
}

/* Entry point that configures tasks, runs scheduler, and prints final stats. */
int main(void) {
    SharedBuffer shared_buffer;

    TCB producer_tcb = {1, 1, READY, producer_task, NULL, 0U};
    TCB consumer_tcb = {2, 1, READY, consumer_task, NULL, 0U};
    TCB logger_tcb = {3, 1, READY, logger_task, NULL, 0U};
    TCB monitor_tcb = {4, 1, READY, monitor_task, NULL, 0U};

    ProducerTaskContext producer_context = {&producer_tcb, &shared_buffer, 1};
    ConsumerTaskContext consumer_context = {&consumer_tcb, &shared_buffer, 0};
    LoggerTaskContext logger_context = {&logger_tcb, &shared_buffer, 0U};
    MonitorTaskContext monitor_context = {&monitor_tcb, &shared_buffer, 0U, DEMO_RUNTIME_SECONDS};

    if (initialize_shared_buffer(&shared_buffer) != 0) {
        (void)printf("Failed to initialize shared resources.\n");
        return 1;
    }

    producer_tcb.arg = &producer_context;
    consumer_tcb.arg = &consumer_context;
    logger_tcb.arg = &logger_context;
    monitor_tcb.arg = &monitor_context;

    if (scheduler_init() != 0) {
        (void)printf("Failed to initialize scheduler.\n");
        destroy_shared_buffer(&shared_buffer);
        return 1;
    }

    if (scheduler_add_task(&producer_tcb) != 0 ||
        scheduler_add_task(&consumer_tcb) != 0 ||
        scheduler_add_task(&logger_tcb) != 0 ||
        scheduler_add_task(&monitor_tcb) != 0) {
        (void)printf("Failed to register tasks with scheduler.\n");
        destroy_shared_buffer(&shared_buffer);
        return 1;
    }

    scheduler_run();

    (void)printf("\nFinal scheduler summary:\n");
    scheduler_stats();
    mem_report();

    destroy_shared_buffer(&shared_buffer);
    return 0;
}
