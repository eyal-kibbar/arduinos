#ifndef ARDUINOS_H_
#define ARDUINOS_H_

#ifdef __cplusplus
extern "C" {
#endif


#define ARDUINOS_NR_CONTEXTS 5
#define ARDUINOS_STACK_SZ 256

typedef int (*context_start_func)(void*);

struct arduinos_semaphore_t {
	int sem_count;
	void* sem_waiting_first;
	void* sem_waiting_last;
};
typedef int cid;

void arduinos_setup();
void arduinos_loop();

cid  arduinos_create(context_start_func func, void* arg);
void arduinos_delay(int milliseconds);
void arduinos_yield();
int  arduinos_join(cid context_id, int* ret);
cid  arduinos_self();
int  arduinos_kill(cid context_id);

void arduinos_semaphore_init(struct arduinos_semaphore_t* sem, int count);
void arduinos_semaphore_wait(struct arduinos_semaphore_t* sem);
void arduinos_semaphore_signal(struct arduinos_semaphore_t* sem);

#ifdef __cplusplus
}
#endif


#endif

