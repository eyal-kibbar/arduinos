#ifndef HEAP_H_
#define HEAP_H_

#define HEAP_MAX_SIZE 10

#ifdef __cplusplus
extern "C" {
#endif
struct heap_t {
	int size;
	int (*compare)(void* a, void* b);
	void* arr[HEAP_MAX_SIZE];
};

int heap_init(struct heap_t* heap,
	int (*compare)(void*, void*));
int heap_peek(struct heap_t* heap, void** elmt);
int heap_push(struct heap_t* heap, void* elmt);
int heap_pop(struct heap_t* heap);

void heap_foreach(struct heap_t* heap, void (*f)(void* elmt));

#ifdef __cplusplus
}
#endif

#endif

