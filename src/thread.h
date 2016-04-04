/*	thread.h
	Copyright (C) 2009-2016 Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

typedef struct tcb tcb;
typedef struct threaddata threaddata;

//	Thread function type
typedef void (*thread_func)(tcb *thread);

//	Thread control block
struct tcb {
	volatile int stop;	// Signal to stop
	volatile int progress;	// Progress counter
	volatile int stopped;	// Stopped state
	int index;		// Thread index
	int count;		// Number of threads
	int step0, nsteps;	// Work allocated to this thread
	int tsteps;		// Total amount of work - set only for thread 0
	threaddata *tdata;	// Pointer to array header
	void *data;		// Parameters & buffers structure for function
};

//	Thread array header
struct threaddata {
	volatile int done;	// Allocated amount of work
	int total;		// Total amount of work
	int count;		// Number of threads
	int chunks;		// Number of chunks per thread
	int silent;		// No progressbar & error window
	thread_func what;	// Function to run
	tcb *threads[1];	// Threads' TCBs
};

//	Configure max number of threads to launch
int maxthreads;

//	Prepare memory structures for threads' use
threaddata *talloc(int flags, int tmax, void *data, int dsize, ...);
//	Launch threads and wait for their exiting
int launch_threads(thread_func thread, threaddata *tdata, char *title, int total);

#ifdef U_THREADS

//	Show threading status
int threads_running;

//	Estimate how many threads is enough for image
int image_threads(int w, int h);
//	Update progressbar from main thread
int thread_progress(tcb *thread);

//	Track a thread's progress
static inline int thread_step(tcb *thread, int i, int tlim, int steps)
{
	thread->progress = i;
	if (thread->index) return (thread->stop);
	if ((i * steps) % tlim < tlim - steps) return (FALSE);
	return (thread_progress(thread));
}

//	Report that thread's work is done
static inline void thread_done(tcb *thread)
{
	thread->stopped = TRUE;
}

//	Define a static mutex
#define	DEF_MUTEX(name) static GStaticMutex name = G_STATIC_MUTEX_INIT
//	Lock a static mutex
#define LOCK_MUTEX(name) \
	if (threads_running) g_static_mutex_lock(&name)
//	Unlock a static mutex
#define UNLOCK_MUTEX(name) \
	if (threads_running) g_static_mutex_unlock(&name)

#ifdef __G_ATOMIC_H__
#define thread_xadd(A,B) g_atomic_int_exchange_and_add((A), (B))
#elif HAVE__SFA
#define thread_xadd(A,B) __sync_fetch_and_add((A), (B))
#else
int thread_xadd(volatile int *var, int n);
#endif

#else /* Only one actual thread */

#define image_threads(w,h) 1

static inline int thread_step(tcb *thread, int i, int tlim, int steps)
{
	if ((i * steps) % tlim < tlim - steps) return (FALSE);
	return (progress_update((float)i / tlim));
}

#define thread_done(thread)

#define	DEF_MUTEX(name)
#define LOCK_MUTEX(name)
#define UNLOCK_MUTEX(name)

static inline int thread_xadd(int *var, int n)
{
	int v = *var;
	*var += n;
	return (v);
}

#endif
