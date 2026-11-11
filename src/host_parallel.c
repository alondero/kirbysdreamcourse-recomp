#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "host_parallel.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Mutex;
typedef CONDITION_VARIABLE Condition;
static void Lock(Mutex *m) { EnterCriticalSection(m); }
static void Unlock(Mutex *m) { LeaveCriticalSection(m); }
static void Wait(Condition *c, Mutex *m) { SleepConditionVariableCS(c, m, INFINITE); }
static void WakeAll(Condition *c) { WakeAllConditionVariable(c); }
static void Relax(void) { SwitchToThread(); }
static unsigned Processors(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;
static void Lock(Mutex *m) { pthread_mutex_lock(m); }
static void Unlock(Mutex *m) { pthread_mutex_unlock(m); }
static void Wait(Condition *c, Mutex *m) { pthread_cond_wait(c, m); }
static void WakeAll(Condition *c) { pthread_cond_broadcast(c); }
static void Relax(void) { sched_yield(); }
static unsigned Processors(void)
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (unsigned)count : 1;
}
#endif

/* kDefaultThreads caps the automatic choice: on a 24-thread CPU, 12 workers
 * drew orbit presents about 15% faster than 8 and left half the processors to
 * the emulator, audio and the system. */
enum { kMaxThreads = 16, kDefaultThreads = 12 };

static struct {
    Mutex lock;
    Condition wake;
    /* The current job. Written under the lock while no worker holds it. */
    KdcParallelTask *task;
    void *context;
    unsigned count, generation;
    /* Workers between reading the job and leaving its loop. */
    unsigned inside;
    atomic_uint next, finished;
    unsigned threads;
    bool started;
} pool;

static void Run(KdcParallelTask *task, void *context, unsigned count)
{
    for (;;) {
        unsigned index = atomic_fetch_add(&pool.next, 1);
        if (index >= count) return;
        task(context, index);
        atomic_fetch_add(&pool.finished, 1);
    }
}

#ifdef _WIN32
static DWORD WINAPI Worker(LPVOID unused)
#else
static void *Worker(void *unused)
#endif
{
    (void)unused;
    unsigned seen = 0;
    Lock(&pool.lock);
    for (;;) {
        while (pool.generation == seen) Wait(&pool.wake, &pool.lock);
        seen = pool.generation;
        KdcParallelTask *task = pool.task;
        void *context = pool.context;
        unsigned count = pool.count;
        ++pool.inside;
        Unlock(&pool.lock);
        Run(task, context, count);
        Lock(&pool.lock);
        --pool.inside;
    }
    return 0;
}

static void Start(void)
{
    pool.started = true;
    unsigned threads = Processors() / 2;
    const char *forced = getenv("KDC_THREADS");
    if (forced && *forced) threads = (unsigned)strtoul(forced, NULL, 10);
    else if (threads > kDefaultThreads) threads = kDefaultThreads;
    if (threads < 1) threads = 1;
    if (threads > kMaxThreads) threads = kMaxThreads;
    pool.threads = 1;
    if (threads == 1) return;
#ifdef _WIN32
    InitializeCriticalSection(&pool.lock);
    InitializeConditionVariable(&pool.wake);
    for (unsigned i = 1; i < threads; ++i) {
        HANDLE thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (!thread) break;
        CloseHandle(thread);
        ++pool.threads;
    }
#else
    pthread_mutex_init(&pool.lock, NULL);
    pthread_cond_init(&pool.wake, NULL);
    for (unsigned i = 1; i < threads; ++i) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, Worker, NULL)) break;
        pthread_detach(thread);
        ++pool.threads;
    }
#endif
}

unsigned KdcParallelThreads(void)
{
    if (!pool.started) Start();
    return pool.threads;
}

void KdcParallelFor(unsigned count, KdcParallelTask *task, void *context)
{
    if (!count || !task) return;
    if (KdcParallelThreads() == 1 || count == 1) {
        for (unsigned i = 0; i < count; ++i) task(context, i);
        return;
    }
    Lock(&pool.lock);
    /* A worker that woke late may still hold the previous, finished job. */
    while (pool.inside) {
        Unlock(&pool.lock);
        Relax();
        Lock(&pool.lock);
    }
    pool.task = task;
    pool.context = context;
    pool.count = count;
    atomic_store(&pool.next, 0);
    atomic_store(&pool.finished, 0);
    ++pool.generation;
    Unlock(&pool.lock);
    WakeAll(&pool.wake);
    Run(task, context, count);
    /* The remaining pieces are already running on other threads and are
     * short; sleeping here would cost more than it saves. */
    for (unsigned spins = 0; atomic_load(&pool.finished) < count; ++spins)
        if (spins > 64) Relax();
}
