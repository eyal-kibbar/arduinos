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
	CTX_WAITING
};

struct context_t {
	jmp_buf            ctx_regs;
	cid                ctx_id;
	enum context_state ctx_state;
	int                ctx_ret;
	struct context_t*  ctx_joining;
	union {
		unsigned long     ctx_ts;
		struct context_t* ctx_next;
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
static struct context_t* active_q;
static struct context_t* standby_q_first;
static struct context_t* standby_q_last;
static struct context_t* curr_ctx;
static struct context_stack_t* free_ctx;
static struct heap_t delay_q;
static struct context_t scheduler_ctx;
static struct context_stack_t contexts[ARDUINOS_NR_CONTEXTS];

// static declerations
static void context_start();
static int  context_compare(struct context_t* ctx1, struct context_t* ctx2);
static void context_schedule(struct context_t* ctx, int delay);
static void context_switch(struct context_t* next_ctx);
static void context_start();
static void context_end(int ret);

static int context_compare(struct context_t* ctx1, struct context_t* ctx2)
{
	if (ctx1->ctx_ts > ctx2->ctx_ts)
		return 1;
	if (ctx1->ctx_ts < ctx2->ctx_ts)
		return -1;
	return 0;
}

static void context_schedule(struct context_t* ctx, int delay)
{
	if (delay) {
		ctx->ctx_ts = millis() + delay;
		ctx->ctx_state = CTX_DELAYED;
		heap_push(&delay_q, ctx);
	} else {
		if (!standby_q_last) {
			standby_q_first = ctx;
		} else {
			standby_q_last->ctx_next = ctx;
		}
		standby_q_last = ctx;
		ctx->ctx_next = NULL;
		ctx->ctx_state = CTX_SCHEDULED;
	}
}

static void context_switch(struct context_t* next_ctx)
{
	if (!setjmp(&curr_ctx->ctx_regs)) {
		curr_ctx = next_ctx;
		if (curr_ctx->ctx_state == CTX_ZOMBIE) {
			context_end(-1);
			// does not return
		}
		curr_ctx->ctx_state = CTX_RUNNING;
		longjmp(&curr_ctx->ctx_regs, 1);
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
	for (ctx = curr_ctx->ctx_joining; ctx; ctx = ctx->ctx_next) {
		ctx->ctx_ret = ret;
		context_schedule(ctx, 0);
	}

	// push back to the free list
	curr_ctx->ctx_next = (struct context_t*)free_ctx;
	free_ctx = (struct context_stack_t*)curr_ctx;
	curr_ctx->ctx_state = CTX_FREE;

	context_switch(&scheduler_ctx);
	// does not return
}

void arduinos_setup()
{
	int i;
	heap_init(&delay_q, context_compare);
	memset(&scheduler_ctx, 0, sizeof scheduler_ctx);
	for (i=0; i < ARDUINOS_NR_CONTEXTS-1; ++i) {
		contexts[i].ctx.ctx_next = &contexts[i+1].ctx;
		contexts[i].ctx.ctx_id = i+1;
	}
	free_ctx = &contexts[0];
	contexts[ARDUINOS_NR_CONTEXTS-1].ctx.ctx_next = NULL;
	contexts[ARDUINOS_NR_CONTEXTS-1].ctx.ctx_id = ARDUINOS_NR_CONTEXTS;
	active_q = NULL;
	standby_q_first = NULL;
	standby_q_last = NULL;
	curr_ctx = NULL;
}

void arduinos_loop()
{
	struct context_t* next_ctx;
	unsigned long now;

	// swap active and standby queues
	active_q = standby_q_first;
	standby_q_first = standby_q_last = NULL;

	curr_ctx = &scheduler_ctx;
	while (active_q) {
		next_ctx = active_q;
		active_q = active_q->ctx_next;
		context_switch(next_ctx);
	}
	curr_ctx = NULL;

	// schedule all the finished delayed contexts
	while (!heap_peek(&delay_q, &next_ctx)) {
		// if we have a zombie context on top, schedule it
		if (next_ctx->ctx_state == CTX_ZOMBIE) {
			heap_pop(&delay_q);
			context_schedule(next_ctx, 0);
			// fix ctx state, context_switch will handle it
			next_ctx->ctx_state = CTX_ZOMBIE;
		// if the top context's timestamp has expired, schedule it
		} else if (next_ctx->ctx_ts <= millis()) {
			heap_pop(&delay_q);
			context_schedule(next_ctx, 0);
		// we still have time for the top context to wait
		} else {
			break;
		}
	}

	
	// check if we can delay the execution
	if (!standby_q_first && !heap_peek(&delay_q, &next_ctx)) {
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
		return -1;
	free_ctx = (struct context_stack_t*)free_ctx->ctx.ctx_next;
	ctx->ctx.ctx_func = func;
	ctx->ctx.ctx_arg = arg;
	ctx->ctx.ctx_joining = NULL;

	sp = ((void*)ctx) + sizeof *ctx - sizeof(int);
	if (setjmp(ctx->ctx.ctx_regs)) {
		// DO NOT TOUCH LOCAL VARS
		// THEY ARE NOT WHERE YOU THINK THEY ARE
		context_start();
		// does not return
	}
	// fix the stack pointer to use the new context's stack
	((struct avr_jmp_buf*)ctx->ctx.ctx_regs)->sp = (uint16_t)sp | (sizeof(int)-1);
	// make sure we are not going to use bp when we start
	((struct avr_jmp_buf*)ctx->ctx.ctx_regs)->bp = 0;
	// schedule the new context to run in the next epoch
	context_schedule(&ctx->ctx, 0);
	return ctx->ctx.ctx_id;
}

void arduinos_delay(int milliseconds)
{
	context_schedule(curr_ctx, milliseconds);
	context_switch(&scheduler_ctx);
}

void arduinos_yield()
{
	arduinos_delay(0);
}

cid arduinos_self()
{
	return curr_ctx->ctx_id;
}

int arduinos_join(cid context_id, int* ret)
{
	struct context_t* ctx;
	ctx = (struct context_t*)&contexts[context_id-1];
	curr_ctx->ctx_next = ctx->ctx_joining;
	ctx->ctx_joining = curr_ctx;
	curr_ctx->ctx_state = CTX_JOINING;
	context_switch(&scheduler_ctx);
	if (ret)
		*ret = curr_ctx->ctx_ret;
	return 0;
}

int arduinos_kill(cid context_id)
{
	contexts[context_id-1].ctx.ctx_state = CTX_ZOMBIE;
	return 0;
}

void arduinos_semaphore_init(struct arduinos_semaphore_t* sem, int count)
{
	sem->sem_count = count;
	sem->sem_waiting_first = NULL;
	sem->sem_waiting_last = NULL;
}
void arduinos_semaphore_wait(struct arduinos_semaphore_t* sem)
{
	if (sem->sem_count) {
		--sem->sem_count;
		return;
	}

	if (!sem->sem_waiting_last) {
		sem->sem_waiting_first = curr_ctx;
	} else {
		((struct context_t*)sem->sem_waiting_last)->ctx_next = curr_ctx;
	}
	sem->sem_waiting_last = curr_ctx;
	curr_ctx->ctx_next = NULL;
	curr_ctx->ctx_state = CTX_WAITING;
	context_switch(&scheduler_ctx);	
}

void arduinos_semaphore_signal(struct arduinos_semaphore_t* sem)
{
	struct context_t* ctx;
	++sem->sem_count;
	if (!sem->sem_waiting_first)
		return;

	ctx = sem->sem_waiting_first;
	if (!(sem->sem_waiting_first = ctx->ctx_next))
		sem->sem_waiting_last = NULL;
	--sem->sem_count;
	context_schedule(ctx, 0);
}



