/* ROM-free stress check for the presentation worker pool: every index of
 * every job runs exactly once, the caller sees all of a job's writes when it
 * returns, and back-to-back jobs of different sizes never leak into each
 * other. */
#include "host_parallel.h"

#include <stdio.h>
#include <string.h>

enum { kMaxCount = 512, kJobs = 4000 };

typedef struct Job {
    unsigned id;
    unsigned char runs[kMaxCount];
    unsigned values[kMaxCount];
} Job;

static void Task(void *context, unsigned index)
{
    Job *job = context;
    ++job->runs[index];
    job->values[index] = job->id * 1000u + index;
}

int main(void)
{
    static Job job;
    if (KdcParallelThreads() < 1) {
        fprintf(stderr, "no threads\n");
        return 1;
    }
    for (unsigned id = 1; id <= kJobs; ++id) {
        const unsigned count = 1 + (id * 37u) % kMaxCount;
        memset(job.runs, 0, sizeof(job.runs));
        job.id = id;
        KdcParallelFor(count, Task, &job);
        for (unsigned i = 0; i < kMaxCount; ++i) {
            const unsigned expected_runs = i < count;
            if (job.runs[i] != expected_runs ||
                (expected_runs && job.values[i] != id * 1000u + i)) {
                fprintf(stderr, "job %u (%u pieces): index %u ran %u times, value %u\n",
                        id, count, i, job.runs[i], job.values[i]);
                return 1;
            }
        }
    }
    printf("host parallel: %u jobs on %u threads passed\n", kJobs, KdcParallelThreads());
    return 0;
}
