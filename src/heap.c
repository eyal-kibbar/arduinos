#include "heap.h"
#include <string.h>

static void heap_swap(struct heap_t* heap, int i, int j)
{
	void* tmp;
	tmp = heap->arr[i];
	heap->arr[i] = heap->arr[j];
	heap->arr[j] = tmp;
}

int heap_init(struct heap_t* heap, compare_func compare)
{
	heap->size = 0;
	heap->compare = compare;
}

int heap_peek(struct heap_t* heap, void** elmt)
{
	if (!heap->size)
		return -1;
	*elmt = heap->arr[0];
	return 0;
}


int heap_push(struct heap_t* heap, void* elmt)
{
	int i, j;
	if (heap->size == HEAP_MAX_SIZE)
		return -1;
	heap->arr[heap->size++] = elmt;
	
	// bubble up
	for (i=heap->size; i > 1 && heap->compare(heap->arr[i/2-1], heap->arr[i-1]) > 0; i /= 2) {
		heap_swap(heap, i-1, i/2-1);
	}

}

int heap_pop(struct heap_t* heap)
{
	int i;
	if (!heap->size)
		return -1;
	heap->arr[0] = heap->arr[--heap->size];

	// bubble down
	for (i=1; (i*2) < heap->size;) {
		// try not to move
		if (heap->compare(heap->arr[i-1], heap->arr[i*2-1]) < 0 &&
		    heap->compare(heap->arr[i-1], heap->arr[i*2])   < 0) {
			break;
		}
		if (heap->compare(heap->arr[i*2], heap->arr[i*2-1]) > 0) {
			heap_swap(heap, i-1, i*2-1);
			i = i*2;
		} else {
			heap_swap(heap, i-1, i*2);
			i = i*2 + 1;
		}
	}
	if (i*2 == heap->size && heap->compare(heap->arr[i-1], heap->arr[i*2-1]) > 0)
		heap_swap(heap, i-1, i*2-1);
	return 0;
}



void heap_foreach(struct heap_t* heap, void (*f)(void* elmt))
{
	int i;
	for (i=0; i < heap->size; f(heap->arr[i++]));
}

