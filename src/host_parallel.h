#ifndef KDC_HOST_PARALLEL_H
#define KDC_HOST_PARALLEL_H

/* A small fixed pool for presentation work that splits into independent
 * pieces, such as bands of a frame. The emulated machine never runs here. */
typedef void KdcParallelTask(void *context, unsigned index);

/* Calls task(context, i) once for every i below count and returns when all
 * calls have finished. The calling thread takes part. Pieces are claimed in
 * any order by any thread, so they must not depend on each other. Call it
 * from one thread at a time, never from inside a task. */
void KdcParallelFor(unsigned count, KdcParallelTask *task, void *context);

/* Threads that take part, including the caller: half the logical processors,
 * at most twelve. KDC_THREADS=n overrides it; 1 runs everything inline. */
unsigned KdcParallelThreads(void);

#endif
