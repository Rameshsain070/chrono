![Language: C](https://img.shields.io/badge/Language-C-blue.svg) ![Build: Passing](https://img.shields.io/badge/Build-Passing-brightgreen.svg) ![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg) ![Platform: Linux/macOS](https://img.shields.io/badge/Platform-Linux%2FmacOS-lightgrey.svg)

**chrono** is a preemptive-style task scheduler in C that simulates core RTOS behaviors on Linux/macOS using POSIX threads.
It executes periodic tasks with round-robin fairness and priority-aware dispatch, while exposing scheduler state in a live terminal dashboard. The project is useful for learning OS and embedded fundamentals because it combines scheduling, synchronization, and memory management in one small codebase you can inspect end-to-end. It is impressive from an engineering perspective because it avoids external libraries, implements core primitives directly, and runs entirely on a desktop host with no development board or MCU required.

## Features

- ✅ Round-robin + priority-based preemptive scheduling
- ✅ Custom fixed-size memory pool (no stdlib `malloc`)
- ✅ Mutex and counting semaphore from scratch
- ✅ Producer-consumer demo with deadlock prevention
- ✅ Live CLI dashboard showing task states and CPU time
- ✅ Zero external dependencies (POSIX only)

## Project Structure

```text
chrono/
├── Makefile              # Build, run, and clean targets for the demo executable
├── README.md             # Project documentation and usage guide
├── include/
│   ├── mempool.h         # Memory pool API and allocator configuration constants
│   ├── scheduler.h       # Scheduler API, timing constants, and runtime statistics type
│   ├── sync.h            # Custom mutex/semaphore interfaces and synchronization types
│   └── task.h            # Task control block (TCB) definition and task state enum
└── src/
    ├── main.c            # Producer-consumer demo tasks, dashboard rendering, and app entry point
    ├── mempool.c         # Fixed-size block allocator implementation and memory usage reporting
    ├── scheduler.c       # Task registration, priority+round-robin selection, and dispatch loop
    └── sync.c            # Custom mutex wrapper and counting semaphore implementation
```

## How It Works

### Task Control Block (TCB)
Each task is represented by a `TCB` (`include/task.h`) containing task ID, priority, lifecycle state (`READY`, `RUNNING`, `BLOCKED`, `SUSPENDED`, `DEAD`), function pointer, argument pointer, and accumulated CPU time. This structure acts as the scheduler’s single source of truth for runnable state and accounting.

### Scheduler (round-robin + priority queue)
The scheduler (`src/scheduler.c`) keeps registered `TCB*` entries in an internal table and builds a min-heap of eligible tasks each cycle. Selection is first by priority and then by per-task round-robin sequence to keep fairness among equal-priority tasks. After dispatching one task function, the scheduler updates CPU time, handles state transitions, increments context-switch metrics, and sleeps for a fixed timeslice (`TIME_SLICE_MS`).

### Sync module (mutex + semaphore)
The synchronization layer (`src/sync.c`) provides a project-owned API over POSIX primitives: `CMutex` for mutual exclusion and `CSemaphore` for counting resources. The semaphore state is protected by a mutex and uses a condition variable for signaling, while the demo uses non-blocking decrement behavior (`EAGAIN`) to model cooperative task retries without hard blocking.

### Memory pool (free-list allocator)
The allocator (`src/mempool.c`) uses a fixed 1024-byte pool split into 32-byte blocks and tracks allocation runs with metadata arrays. Allocation scans for a contiguous free run and marks ownership without calling libc heap allocators. Freeing validates pointer bounds/alignment, releases the original run, and allows deterministic memory behavior that is closer to embedded constraints.

### CLI dashboard
The monitor task in `src/main.c` prints a recurring dashboard showing each task’s ID/name/priority/state/CPU time, total context switches, memory usage, and producer-consumer buffer counters. This gives a live view of scheduler dynamics and synchronization health during execution.

## Build & Run

### Prerequisites

- `gcc` (C99 support)
- `make`
- Linux or macOS with POSIX threads (`pthread`)

### Commands

```bash
git clone https://github.com/Rameshsain070/chrono.git
cd chrono
make
make run
make clean
```

### Expected Output (sample)

```text
./chrono
[logger] produced=5 consumed=5 buffered=0

=== Chrono dashboard ===
task ID name       priority  state       cpu_time_ms
1       producer   1         READY       0
2       consumer   1         READY       0
3       logger     1         READY       0
4       monitor    1         RUNNING     0
Context switches: 40
Memory used/total: 0/1024 bytes
Buffer items: 0 | Produced: 10 | Consumed: 10
========================

Final scheduler summary:
[scheduler] Context switches: 600 | Task count: 4
[mempool] Used: 0/1024 bytes | Free runs: 1 | Fragmentation: 0.00%
```

## Concepts Demonstrated

| Concept | Where in Code |
|---|---|
| Scheduling (priority + round-robin) | `src/scheduler.c` (`scheduler_build_heap`, heap push/pop, dispatch loop) |
| IPC / producer-consumer coordination | `src/main.c` (`SharedBuffer`, producer/consumer task flow) |
| Mutex | `include/sync.h` + `src/sync.c` (`CMutex`, `cmutex_*`) |
| Semaphore | `include/sync.h` + `src/sync.c` (`CSemaphore`, `sem_wait`, `sem_signal`) |
| Memory management (fixed pool allocator) | `include/mempool.h` + `src/mempool.c` (`mem_alloc`, `mem_free`, `mem_report`) |
| Function pointers (task dispatch) | `TCB.func` in `include/task.h`, invocation in `src/scheduler.c` |
| Data structures (heap, ring buffer, block map) | Min-heap in `src/scheduler.c`, circular buffer in `src/main.c`, block metadata arrays in `src/mempool.c` |
| POSIX threads primitives | `pthread_mutex_*`, `pthread_cond_*`, `nanosleep` in `src/sync.c`, `src/mempool.c`, `src/scheduler.c` |

## Interview Talking Points

- Built an RTOS-like scheduler simulation from first principles in C, including lifecycle-managed TCBs and deterministic timeslice dispatch.
- Implemented priority-aware round-robin selection with explicit context-switch accounting and per-task CPU-time measurement.
- Designed custom synchronization primitives and used them to solve producer-consumer coordination safely under contention.
- Replaced dynamic heap allocation with a fixed-block memory pool to model embedded memory constraints and fragmentation visibility.
- Delivered an observable runtime dashboard that exposes scheduling, memory, and synchronization metrics for debugging and performance discussion.

## License

MIT License.
