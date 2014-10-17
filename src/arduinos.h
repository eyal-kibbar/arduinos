#ifndef ARDUINOS_H_
#define ARDUINOS_H_

#ifdef __cplusplus
extern "C" {
#endif


#define ARDUINOS_NR_CONTEXTS 4
#define ARDUINOS_STACK_SZ 180

enum arduinos_status {
	ARDUINOS_STATUS_SUCCESS = 0,
	ARDUINOS_STATUS_INVALID = -1,
	ARDUINOS_STATUS_KILLED = -2,
	ARDUINOS_STATUS_RESRC_EXHAUSTED = -3,
	ARDUINOS_STATUS_SEM_DESTROYED = -4
};

struct context_t;

struct context_queue_t {
	struct context_t* q_first;
	struct context_t* q_last;
};

struct arduinos_semaphore_t {
	int sem_count;
	struct context_queue_t sem_q;
};

typedef int cid;
typedef int (*context_start_func)(void*);

void arduinos_setup();
void arduinos_loop();

cid  arduinos_create(context_start_func func, void* arg);
void arduinos_delay(int milliseconds);
void arduinos_yield();
int  arduinos_join(cid context_id, int* ret);
cid  arduinos_self();
int  arduinos_kill(cid context_id);
int  arduinos_pause(cid context_id);
int  arduinos_resume(cid context_id);

void arduinos_semaphore_init(struct arduinos_semaphore_t* sem, int count);
void arduinos_semaphore_fini(struct arduinos_semaphore_t* sem);
void arduinos_semaphore_signal(struct arduinos_semaphore_t* sem);
int arduinos_semaphore_wait(struct arduinos_semaphore_t* sem);

#ifdef __cplusplus
}
#endif


#endif

