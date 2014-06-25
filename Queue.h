/*
 * Queue.h
 *
 *  Created on: Oct 25, 2013
 *      Author: subhranil
 */
#include<stddef.h>
#include<stdlib.h>

#ifndef QUEUE_H_
#define QUEUE_H_

struct task;

typedef struct Node
{
	void* data;
	struct Node* next;
} Node;

//class Queue {
//protected:
//
//	Node *mHead;
//	Node *mTail;
//	int mNumberOfElements;
//public:
//	Queue();
//	virtual ~Queue();
//	Node* start();
//	bool isEmpty();
//	virtual void enQueue(void*);
//	Node *deQueue();
//	static Node* iterate(Node* head);
//
//};
//
//
//
//class PriorityQueue: public Queue
//{
//public:
//	PriorityQueue();
//	void enQueue(void* task);
//};

typedef struct Queue
{
	Node *mHead;
	Node *mTail;
	int mNumberOfElements;
//public:
//	Queue();
//	virtual ~Queue();
//	Node* start();
//	bool isEmpty();
//	virtual void enQueue(void*);
//	Node *deQueue();
//	static Node* iterate(Node* head);
} Queue;

Queue* initialiseQueue();
void enQueue(Queue*,void*);
Node* deQueue(Queue*);
//static Node* iterate(Queue,Node* head);

#endif /* QUEUE_H_ */


