/*	thread.h
	Copyright (C) 2009 Dmitry Groshev

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

//	Thread control block
typedef struct tcb tcb;
struct tcb {
	volatile int stop;	// Signal to stop
	volatile int progress;	// Progress counter
	volatile int stopped;	// Stopped state
	int index;		// Thread index
	int count;		// Number of threads
	int step0, nsteps;	// Work allocated to this thread
	int tsteps;		// Total amount of work - set only for thread 0
	tcb **threads;		// Pointers to all tcbs
	void *data;		// Parameters & buffers structure for function
};

//	Thread array header
typedef struct {
	int count;
	tcb *threads[1];
} threaddata;

//	Thread function type
typedef void (*thread_func)(tcb *thread);

//	Configure max number of threads to launch
int maxthreads;

//	Estimate how many threads is enough for image
int image_threads(int w, int h);
//	Prepare memory structures for threads' use
threaddata *talloc(int flags, int tmax, void *data, int dsize, ...);
//	Launch threads and wait for their exiting
void launch_threads(thread_func thread, threaddata *tdata, char *title, int total);

#ifdef U_THREADS

//	Show threading status
int threads_running;

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

#else /* Only one actual thread */

static inline int thread_step(tcb *thread, int i, int tlim, int steps)
{
	if ((i * steps) % tlim < tlim - steps) return (FALSE);
	return (progress_update((float)i / tlim));
}

#define thread_done(thread)

#define	DEF_MUTEX(name)
#define LOCK_MUTEX(name)
#define UNLOCK_MUTEX(name)

#endif
