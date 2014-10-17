#include "arduinos.h"
#include <Arduino.h>
#include <setjmp.h>
#include <stdint.h>
#include "heap.h"

struct avr_jmp_buf {
	uint8_t  regs[16];
	uint16_t bp;
	uint16_t sp;
	uint8_t  sreg;
	uint16_t pc;
} __attribute__((packed));


enum context_state {
	CTX_FREE,
	CTX_RUNNING,
	CTX_JOINING,
	CTX_SCHEDULED,
	CTX_DELAYED,
	CTX_ZOMBIE,
	CTX_WAITING,
	CTX_PAUSED
};

struct context_t {
	jmp_buf            		ctx_regs;
	cid                		ctx_id;
	enum context_state 		ctx_state;
	int       	         	ctx_ret;
	enum arduinos_status		ctx_ret_status;
	struct context_queue_t 		ctx_joining_q;
	union {
		unsigned long		ctx_ts;
		struct context_t*	ctx_next;
	};
	// must be last: if the stack overflows, these
	// will be the first to go. Since we only use
	// them when the context starts, we can safly
	// overwrite them
	context_start_func ctx_func;
	void*              ctx_arg;
};

struct context_stack_t {
	struct context_t ctx;
	uint8_t stack[ARDUINOS_STACK_SZ - sizeof (struct context_t)];
};


// globals
static struct context_queue_t standby_q;
static struct context_t* active_lst;
static struct context_t* curr_ctx;
static struct context_stack_t* free_ctx;
static struct heap_t delay_q;
static struct context_t scheduler_ctx;
static struct context_stack_t contexts[ARDUINOS_NR_CONTEXTS];

// static declerations
static void ctx_q_init(struct context_queue_t*);
static void ctx_q_enqueue(struct context_queue_t* q, struct context_t* ctx);
static struct context_t* ctx_q_dequeue(struct context_queue_t* q);
static int ctx_q_is_empty(struct context_queue_t*);

static void context_start();
static int  context_compare(struct context_t* ctx1, struct context_t* ctx2);
static void context_schedule(struct context_t* ctx);
static void context_switch(struct context_t* next_ctx);
static void context_start();
static void context_end(int ret);
static struct context_t* cid2ctx(cid context_id);

// ----------------------------------------------------------------------------
// Queue
// ----------------------------------------------------------------------------
static void ctx_q_init(struct context_queue_t* q)
{
	memset(q, 0, sizeof *q);
}

static struct context_t* ctx_q_dequeue(struct context_queue_t* q)
{
	struct context_t* ret;
	if (!q->q_first)
		return NULL;
	ret = q->q_first;
	q->q_first = ret->ctx_next;
	ret->ctx_next = NULL;
	return ret;
}

void ctx_q_enqueue(struct context_queue_t* q, struct context_t* ctx)
{
	if (!q->q_first) {
		q->q_first = ctx;
	} else {
		q->q_last->ctx_next = ctx;
	}
	q->q_last = ctx;
	ctx->ctx_next = NULL;
}

static int ctx_q_is_empty(struct context_queue_t* q)
{
	return q->q_first == NULL;
}
// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------
static int context_compare(struct context_t* ctx1, struct context_t* ctx2)
{
	if (ctx1->ctx_ts > ctx2->ctx_ts)
		return 1;
	if (ctx1->ctx_ts < ctx2->ctx_ts)
		return -1;
	return 0;
}

static void context_schedule(struct context_t* ctx)
{
	ctx_q_enqueue(&standby_q, ctx);
	if (ctx->ctx_state != CTX_ZOMBIE)
		ctx->ctx_state = CTX_SCHEDULED;
}

static void context_switch(struct context_t* next_ctx)
{
	if (!setjmp(curr_ctx->ctx_regs)) {
		curr_ctx = next_ctx;
		if (curr_ctx->ctx_state == CTX_ZOMBIE) {
			context_end(-1);
			// does not return
		}
		curr_ctx->ctx_state = CTX_RUNNING;
		longjmp(curr_ctx->ctx_regs, 1);
	}
}

static void context_start()
{
	context_end(curr_ctx->ctx_func(curr_ctx->ctx_arg));
}

static void context_end(int ret)
{
	struct context_t* ctx;
	// wake all the waiting processes
	while ((ctx = ctx_q_dequeue(&curr_ctx->ctx_joining_q))) {
		ctx->ctx_ret = ret;
		context_schedule(ctx);
	}

	// push back to the free list
	curr_ctx->ctx_next = (struct context_t*)free_ctx;
	free_ctx = (struct context_stack_t*)curr_ctx;
	curr_ctx->ctx_state = CTX_FREE;

	context_switch(&scheduler_ctx);
	// does not return
}

static struct context_t* cid2ctx(cid context_id)
{
	if (context_id <= 0 || ARDUINOS_NR_CONTEXTS < context_id)
		return NULL;
	return &contexts[context_id-1].ctx;
}
// ----------------------------------------------------------------------------
// Arduinos
// ----------------------------------------------------------------------------
void arduinos_setup()
{
	int i;
	for (i=0; i < ARDUINOS_NR_CONTEXTS-1; ++i) {
		contexts[i].ctx.ctx_next = &contexts[i+1].ctx;
		contexts[i].ctx.ctx_id = i+1;
	}
	free_ctx = &contexts[0];
	contexts[ARDUINOS_NR_CONTEXTS-1].ctx.ctx_next = NULL;
	contexts[ARDUINOS_NR_CONTEXTS-1].ctx.ctx_id = ARDUINOS_NR_CONTEXTS;

	memset(&scheduler_ctx, 0, sizeof scheduler_ctx);

	heap_init(&delay_q, (compare_func)context_compare);
	curr_ctx = NULL;
	active_lst = NULL;
	ctx_q_init(&standby_q);
}

void arduinos_loop()
{
	struct context_t* next_ctx;
	unsigned long now;

	// swap active and standby queues
	active_lst = standby_q.q_first;
	ctx_q_init(&standby_q);

	curr_ctx = &scheduler_ctx;
	while (active_lst) {
		next_ctx = active_lst;
		active_lst = active_lst->ctx_next;
		context_switch(next_ctx);
	}
	curr_ctx = NULL;

	// schedule all the finished delayed contexts
	while (!heap_peek(&delay_q, (void**)&next_ctx)) {
		now = millis();
		// if the top context's timestamp has not expired and it is not
		// a zombie context, we don't need to schedule it
		if (now < next_ctx->ctx_ts && next_ctx->ctx_state != CTX_ZOMBIE)
			break;
		
		heap_pop(&delay_q);
		context_schedule(next_ctx);
	}
	
	// check if we can delay the execution
	if (ctx_q_is_empty(&standby_q) && !heap_peek(&delay_q, (void**)&next_ctx)) {
		now = millis();
		if (now < next_ctx->ctx_ts)
			delay(next_ctx->ctx_ts - now);
	}
	// epoch ended
}

cid arduinos_create(context_start_func func, void* arg)
{
	struct context_stack_t* ctx;
	int* sp;

	if (!(ctx = free_ctx))
		return ARDUINOS_STATUS_RESRC_EXHAUSTED;
	free_ctx = (struct context_stack_t*)free_ctx->ctx.ctx_next;

	// init func
	ctx->ctx.ctx_func = func;
	ctx->ctx.ctx_arg = arg;

	// init the joining queue
	ctx_q_init(&ctx->ctx.ctx_joining_q);

	// init stack and other registers
	sp = ((void*)ctx) + sizeof *ctx - sizeof(int*);
	if (setjmp(ctx->ctx.ctx_regs)) {
		// DO NOT TOUCH LOCAL VARS
		// THEY ARE NOT WHERE YOU THINK THEY ARE
		context_start();
		// does not return
	}
	// fix the stack pointer to use the new context's stack
	((struct avr_jmp_buf*)ctx->ctx.ctx_regs)->sp = (uint16_t)sp | (sizeof(int*)-1);
	// make sure we are not going to use bp when we start
	((struct avr_jmp_buf*)ctx->ctx.ctx_regs)->bp = 0;
	// schedule the new context to run in the next epoch
	context_schedule(&ctx->ctx);
	return ctx->ctx.ctx_id;
}

void arduinos_delay(int milliseconds)
{
	curr_ctx->ctx_ts = millis() + milliseconds;
	curr_ctx->ctx_state = CTX_DELAYED;
	heap_push(&delay_q, curr_ctx);
	context_switch(&scheduler_ctx);
}

void arduinos_yield()
{
	context_schedule(curr_ctx);
	context_switch(&scheduler_ctx);
}

cid arduinos_self()
{
	return curr_ctx->ctx_id;
}

int arduinos_join(cid context_id, int* ret)
{
	struct context_t* ctx;
	if (!(ctx = cid2ctx(context_id)))
		return ARDUINOS_STATUS_INVALID;
	
	if (ctx == curr_ctx || ctx->ctx_state == CTX_ZOMBIE || ctx->ctx_state == CTX_FREE)
		return ARDUINOS_STATUS_INVALID;

	ctx_q_enqueue(&ctx->ctx_joining_q, curr_ctx);
	curr_ctx->ctx_state = CTX_JOINING;
	curr_ctx->ctx_ret_status = ARDUINOS_STATUS_SUCCESS;
	context_switch(&scheduler_ctx);
	if (curr_ctx->ctx_ret_status != ARDUINOS_STATUS_SUCCESS)
		return curr_ctx->ctx_ret_status;
	if (ret)
		*ret = curr_ctx->ctx_ret;
	return 0;
}

int arduinos_kill(cid context_id)
{
	struct context_t* ctx;

	if (!(ctx = cid2ctx(context_id)))
		return ARDUINOS_STATUS_INVALID;

	if (ctx->ctx_state == CTX_ZOMBIE || ctx->ctx_state == CTX_FREE)
		return ARDUINOS_STATUS_INVALID;

	// mark the context as zombie, the next time it will be schedule
	// we will remove it completely
	ctx->ctx_state = CTX_ZOMBIE;

	// fail all its joining contexts
	for (ctx = curr_ctx->ctx_joining_q.q_first; ctx; ctx = ctx->ctx_next) {
		ctx->ctx_ret_status = ARDUINOS_STATUS_KILLED;
	}

	// check if this was a suicide
	if (curr_ctx == ctx)
		context_switch(&scheduler_ctx);
	
	return 0;
}

int arduinos_pause(cid context_id)
{
	struct context_t* ctx;

	if (!(ctx = cid2ctx(context_id)))
		return ARDUINOS_STATUS_INVALID;

	if (ctx->ctx_state == CTX_ZOMBIE || ctx->ctx_state == CTX_FREE)
		return ARDUINOS_STATUS_INVALID;

	ctx->ctx_state = CTX_PAUSED;
	if (ctx == curr_ctx)
		context_switch(&scheduler_ctx);

	return 0;
}

int arduinos_resume(cid context_id)
{
	struct context_t* ctx;

	if (!(ctx = cid2ctx(context_id)))
		return ARDUINOS_STATUS_INVALID;

	if (ctx->ctx_state != CTX_PAUSED)
		return ARDUINOS_STATUS_INVALID;

	context_schedule(ctx);
	return 0;
}
// ----------------------------------------------------------------------------
// Semaphore
// ----------------------------------------------------------------------------
void arduinos_semaphore_init(struct arduinos_semaphore_t* sem, int count)
{
	sem->sem_count = count;
	ctx_q_init(&sem->sem_q);
}

void arduinos_semaphore_fini(struct arduinos_semaphore_t* sem)
{
	struct context_t* ctx;
	
	// schedule all the waiting contexts
	while ((ctx = ctx_q_dequeue(&sem->sem_q))) {
		ctx->ctx_ret_status = ARDUINOS_STATUS_SEM_DESTROYED;
		context_schedule(ctx);
	}

}

int arduinos_semaphore_wait(struct arduinos_semaphore_t* sem)
{
	if (sem->sem_count) {
		--sem->sem_count;
		return;
	}
	ctx_q_enqueue(&sem->sem_q, curr_ctx);
	curr_ctx->ctx_state = CTX_WAITING;
	curr_ctx->ctx_ret_status = ARDUINOS_STATUS_SUCCESS;
	context_switch(&scheduler_ctx);
	return curr_ctx->ctx_ret_status;
}

void arduinos_semaphore_signal(struct arduinos_semaphore_t* sem)
{
	struct context_t* ctx;

	++sem->sem_count;

	// flush all the waiting zombies
	while ((ctx = ctx_q_dequeue(&sem->sem_q)) && ctx->ctx_state == CTX_ZOMBIE)
		context_schedule(ctx);

	// stop if the waiting queue is empty
	if (!ctx)
		return;

	// the waiting queue has at least 1 context alive in it
	context_schedule(ctx);
	--sem->sem_count;
}


