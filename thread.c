#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

typedef enum state_t {
	T_FREE,
	T_READY,
	T_RUNNING,
	T_BLOCKED,
	T_EXITED
} state_t;

/* This is the thread control block */
struct thread {
	Tid tid;
	state_t state;
	ucontext_t context;
	struct wait_queue *waiting; // Queue of threads waiting for this one to exit
	void *stack; // malloc'ed stack (for free)
	struct thread *next;
};

typedef struct queue_t {
	state_t qtype;
	struct thread *head; // head of linked list of threads in queue
	int num_threads; // number of threads in queue
} queue_t;

struct wait_queue {
	queue_t q;
};

/* Queue operations */

/* Append: Appends a thread to a queue (on the right), and also changes the state of the thread
to the queue that it has been appended to */
int append(queue_t *queue, struct thread *t){
	assert(!interrupts_enabled());
	t->next = NULL;
	t->state = queue->qtype;
	struct thread *curr = queue->head;
	if (curr == NULL) // queue is empty 
		queue->head = t;
	else{
		while (curr->next != NULL)
			curr = curr->next; // walk to end of list
		curr->next = t;
	}
	++queue->num_threads;
	return 0;
}

/* Remove thread tid from the queue if present, otherwise return NULL */
struct thread* qremove(queue_t* q, Tid tid){
	assert(!interrupts_enabled());
	struct thread *prev = q->head, *curr, *temp;
	// Head holds tid
	if (q->head->tid == tid) {
		curr = q->head;
		temp = curr->next;
		q->head = temp;
		curr->next = NULL;
		(q->num_threads)--;
		return curr;
	}
	while (prev){
		if ((curr = prev->next)->tid == tid){
			prev->next = curr->next;
			curr->next = NULL;
			(q->num_threads)--;
			return curr;	
		}
		prev = prev->next;
	}
	return NULL;
}

/* Pops the first element from a queue */
struct thread* pop(queue_t* q){
	assert(!interrupts_enabled());
	struct thread* curr = q ? q->head : NULL;
	if (q && curr){
		q->head = curr->next;
		curr->next = NULL;	
		(q->num_threads)--;
		return curr;
	}
	return NULL; 
}

queue_t running = {T_RUNNING},
		ready = {T_READY},
		exited = {T_EXITED};

// Array of all threads ordered by tid
struct thread thread_table[THREAD_MAX_THREADS];
Tid num_threads = 0; 

/* Removes a thread from the queue that it is currently in */
int qremove_from(struct thread* t){
	assert(!interrupts_enabled());
	switch (t->state) {
	case T_READY  : qremove(&ready  , t->tid); break;
	case T_EXITED : qremove(&exited , t->tid); break;
	default: return 1;
	}
	return 0;
}

void debug_info();
void debug_queues();
void debug_wait_queue(struct wait_queue *q);

void flush_exit_queue(){
	assert(!interrupts_enabled());
	struct thread *t = exited.head;
	while (t != NULL){
		thread_kill(t->tid);
		// Thread kill modifies the exited thread, so reremove the new head
		t = exited.head;
	}
}

void debug_info(){
	struct thread* t;
	printf("\n============== On thread: (%d)===================\n", thread_id());
	printf("READY (%d): ", ready.num_threads);
	t = ready.head;
	while (t){
		printf("%d -> ", t->tid);
		t = t->next;
	}
	printf("NULL\n");
	printf("------------------------------------------------\n");
	printf("RUNNING (%d): ", running.num_threads);
	t = running.head;
	while (t){
		printf("%d -> ", t->tid);
		t = t->next;
	}
	printf("NULL\n");
	printf("------------------------------------------------\n");
	printf("EXITED (%d): ", exited.num_threads);
	t = exited.head;
	while (t){
		printf("%d -> ", t->tid);
		t = t->next;
	}
	printf("NULL\n");
	printf("===============END OF DEBUG INFO================\n\n");
	}

void debug_queues(){
	struct thread* t;
	printf("\n============== On thread: (%d)===================\n", thread_id());
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		t = &thread_table[i];
		printf("WAIT %d (%d): ", i, (t->waiting != NULL) ? t->waiting->q.num_threads : 0);
		t = (t->waiting != NULL) ? t->waiting->q.head : NULL;
		while (t){
			printf("[%d/%c] -> ", t->tid, (t->state == T_BLOCKED) ? 'B': 'E');
			t = t->next;
		}
		printf("NULL\n");
		printf("------------------------------------------------\n");
	}
		printf("===============END OF DEBUG INFO================\n\n");
	}


void debug_wait_queue(struct wait_queue* q)
{
	printf("\n==============WAIT QUEUE @ (%p)==================\n", q);
	printf("WAIT QUEUE (%d): ", q->q.num_threads);
	struct thread *t = q->q.head;
	while (t){
		printf("%d -> ", t->tid);
		t = t->next;
	}
	printf("NULL\n");
	printf("===============END OF DEBUG INFO=================\n\n");

}

void
thread_init(void)
{
	// Init thread_table
	struct thread *curr;
	for (int tid = 0; tid < THREAD_MAX_THREADS; tid++){
		curr = &thread_table[tid];
		curr->tid = tid;
		curr->next = NULL;
		curr->stack = NULL;
		curr->state = T_FREE;
		curr->waiting = NULL;
	}
	// Create main thread
	getcontext(&thread_table[0].context);
	interrupts_off();
	append(&running, &thread_table[0]);
	thread_table[0].waiting = wait_queue_create();
	interrupts_on();
	num_threads++;
}

Tid
thread_id()
{
	return running.head->tid;
}

/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit();
}

Tid
_thread_create(void (*fn) (void *), void *parg)
{
	if (num_threads == THREAD_MAX_THREADS)
		return THREAD_NOMORE;
	
	// Get Tid for new thread
	Tid tid = 0;
	while (thread_table[tid % THREAD_MAX_THREADS].state != T_FREE)
		tid++;
	// Malloc stack
	void *stack = malloc(THREAD_MIN_STACK);
	if (stack == NULL)
		return THREAD_NOMEMORY;
	
	thread_table[tid].stack = stack;
	thread_table[tid].waiting = wait_queue_create();
	// Set context of thread
	ucontext_t *context = &thread_table[tid].context;
	getcontext(context);
	
	// Modify key registers
	context->uc_mcontext.gregs[REG_RSP] = (greg_t) (stack + THREAD_MIN_STACK - 0x8);
	context->uc_mcontext.gregs[REG_RIP] = (greg_t) thread_stub;
	context->uc_mcontext.gregs[REG_RDI] = (greg_t) fn; 
	context->uc_mcontext.gregs[REG_RSI] = (greg_t) parg; 
	
	// Add to ready queue
	append(&ready, &thread_table[tid]);
	num_threads++;
	return tid;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	Tid ret = _thread_create(fn, parg);
	interrupts_set(intr_enabled);
	return ret;
}

Tid
_thread_yield(Tid want_tid)
{	
	volatile bool context_changed = false;
	volatile Tid ntid = want_tid;
	// yield wants the running thread 
	if (want_tid == thread_id() || want_tid == THREAD_SELF)
		return thread_id();
	
	struct thread *nt, *ct; 
	if (want_tid == THREAD_ANY) // get thread from FIFO
		nt = pop(&ready);
	// get specific thread
	else if (want_tid > 0 && want_tid < THREAD_MAX_THREADS) {
		nt = &thread_table[want_tid];
		if (nt->state != T_READY)
			return THREAD_INVALID;
		nt = qremove(&ready, want_tid); 
	} else 
		return THREAD_INVALID;
	
	if (nt == NULL) // There are no threads left 
		return THREAD_NONE; 
		
	// Perform thread-switch
	ntid = nt->tid;	
	ct = running.head;
	if (ct->state != T_EXITED && ct->state != T_BLOCKED)
		append(&ready, ct);
	getcontext(&ct->context);
	
	if (context_changed){
		flush_exit_queue();
		return ntid;
	}

	context_changed = true;
	running.head = nt;
	nt->state = T_RUNNING;
	setcontext(&nt->context);
	assert(false);
	return want_tid;
}

Tid
thread_yield(Tid want_tid)
{
	/* mutex'd thread yield */
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	Tid ret = _thread_yield(want_tid);
	interrupts_set(intr_enabled);
	return ret;
}

void
thread_exit()
{
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled()); // Critical section
	struct thread *t = running.head;
	thread_wakeup(t->waiting, 1); // Wake up all waiting threads

	if (ready.num_threads == 0){
		wait_queue_destroy(t->waiting);
		exit(0);
	}
	
	t->state = T_EXITED;
	append(&exited, t); 
	interrupts_set(intr_enabled);
	thread_yield(THREAD_ANY);
}

Tid
_thread_kill(Tid tid)
{
	struct thread* t;
	// Check if requested thread is valid
	if (tid != thread_id() && 
	   (tid >= 0 && tid < THREAD_MAX_THREADS) &&  
	   (t = &thread_table[tid])->state != T_FREE)
	{
		if (t->state == T_BLOCKED){ // Attempting to kill a blocked thread
			t->state = T_EXITED; // Kill it later when it wakes up
			return tid;
		}
		qremove_from(t);
		free(t->stack);
		wait_queue_destroy(t->waiting);
		t->waiting = NULL;
		t->state = T_FREE;
		num_threads--;
		return tid;
	}
	return THREAD_INVALID;
}

Tid 
thread_kill(Tid tid)
{
	/* mutex'd thread kill */
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	Tid ret = _thread_kill(tid);
	interrupts_set(intr_enabled);
	return ret;
}

struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);
	
	wq->q.qtype = T_BLOCKED;
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	thread_wakeup(wq, 1);
	free(wq);
}

Tid
_thread_sleep(struct wait_queue *queue)
{
	if (queue == NULL)
		return THREAD_INVALID;
	else if (ready.num_threads == 0)
		return THREAD_NONE;

	append(&queue->q, running.head);    // Add current thread to wait queue
	Tid ret = thread_yield(THREAD_ANY); // yield to any other available thread
	return ret; 
}

Tid
thread_sleep(struct wait_queue *queue)
{
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	Tid ret = _thread_sleep(queue);
	interrupts_set(intr_enabled);
	return ret;
}
/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns number of threads that woke up. */
int
_thread_wakeup(struct wait_queue *queue, int all)
{
	volatile int num_wakey = 0, num_sleeping = 0;
	struct thread *t;
	if (queue != NULL && (num_sleeping = queue->q.num_threads) != 0)
		for (int i = 0; i < (all ? num_sleeping: 1); i++){
			if ((t = pop(&queue->q))->state == T_EXITED)
				append(&exited, t);
			else if (t->state == T_BLOCKED) 
				append(&ready, t);
			else continue;
			num_wakey++;
		}
	return num_wakey;
}

int
thread_wakeup(struct wait_queue *queue, int all)
{
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	int ret = _thread_wakeup(queue, all);
	interrupts_set(intr_enabled);
	return ret;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	struct thread *t;
	if (tid < 0 || tid > THREAD_MAX_THREADS || (t = &thread_table[tid])->state == T_FREE || t->state == T_RUNNING){
		interrupts_set(intr_enabled);
		return THREAD_INVALID;
	}
	// NOTE: if t->state == T_BLOCKED, and t in thread_id()'s wait queue, this creates a deadlock
	Tid ntid = thread_sleep(t->waiting);
	interrupts_set(intr_enabled);
	return (ntid < 0) ? THREAD_INVALID: tid;
}

struct lock {
	Tid holder; // thread that's holding the lock
	struct wait_queue *waiting; // threads waiting for this lock
	bool free;
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->waiting = wait_queue_create();
	lock->free = true;
	lock->holder = thread_id();
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);
	assert(lock->free == true);
	thread_wakeup(lock->waiting, 1);
	wait_queue_destroy(lock->waiting);
	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());

	while(!lock->free)
		thread_sleep(lock->waiting);
	
	lock->holder = thread_id();
	lock->free = false;
	interrupts_set(intr_enabled);
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	assert(lock->holder == thread_id());

	thread_wakeup(lock->waiting, 1);
	lock->holder = -1;
	lock->free = true;
	interrupts_set(intr_enabled);
}

struct cv {
	Tid holding; // Thread holding cv
	struct wait_queue *waiting; // threads waiting on cv
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);
	cv->holding = thread_id();
	cv->waiting = wait_queue_create();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);
	assert(cv->holding == thread_id());
	thread_wakeup(cv->waiting, 1);
	wait_queue_destroy(cv->waiting);
	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->holder == thread_id());
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	
	lock_release(lock);
	thread_sleep(cv->waiting);
	lock_acquire(lock);
	interrupts_set(intr_enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->holder == thread_id());
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	thread_wakeup(cv->waiting, 0);
	interrupts_set(intr_enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	assert(lock->holder == thread_id());
	volatile int intr_enabled = interrupts_off();
	assert(!interrupts_enabled());
	thread_wakeup(cv->waiting, 1);
	interrupts_set(intr_enabled);
}
