#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "queue.h"
#ifndef QUEUE_H_
#define QUEUE_H_
struct Queue{
	int front,rear,size;
	int capacity;
	int *array;
};

struct Queue* createQueue(int capacity);

int queueSize(struct Queue* queue);

int isFull(struct Queue* queue);

int isEmpty(struct Queue* queue);

void enqueue(struct Queue* queue,int controlBlock);

int dequeue(struct Queue* queue);

int front(struct Queue* queue);

int rear (struct Queue* queue);


#endif
struct Queue* createQueue(int capacity){
	struct Queue* queue = (struct Queue*) malloc(sizeof(struct Queue));	
	queue->capacity = capacity;
	queue->front = queue->size = 0;
	queue->rear = capacity - 1;
	queue->array = (int*)malloc(queue->capacity*sizeof(int));
	return queue;
}

int isFull(struct Queue* queue){
	if(queue->size == queue->capacity){
		return 1;
	}else{
		return 0;
	}
}

int isEmpty(struct Queue* queue){
	return(queue->size == 0);
}

void enqueue(struct Queue* queue, int id){
	if(isFull(queue) == 1){return;}
	queue->rear = (queue->rear + 1)%queue->capacity;
	queue->array[queue->rear] = id;
	queue->size = queue->size + 1;

}

int dequeue(struct Queue* queue){
	if(isEmpty(queue)){return INT_MIN;}

	int id = queue->array[queue->front];
	queue->front = (queue->front + 1) % queue->capacity;
	queue->size = queue->size - 1;
	return id;
}
