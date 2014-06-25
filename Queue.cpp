/*
 * Queue.cpp
 *
 *  Created on: Oct 25, 2013
 *      Author: subhranil
 */

#include "Queue.h"

//Queue::Queue() {
//	// TODO Auto-generated constructor stub
//	mHead=NULL;
//	mTail=NULL;
//	mNumberOfElements=0;
//
//}

//Queue::~Queue() {
//	// TODO Auto-generated destructor stub
//}
//
//bool Queue::isEmpty()
//{
//	return (mHead!=NULL);
//}
//void Queue::enQueue(void* data)
//{
//
//	Node *newNode=(Node*)malloc(sizeof(Node));
//	newNode->data=data;
//	newNode->next=NULL;
//	if(mTail) //starting condition
//		mTail->next=newNode;
//	mTail=newNode;
//	mNumberOfElements++;
//	if(!mHead) //starting condition
//		mHead=mTail;
//
//}

//Node* Queue::start()
//{
//	return mHead;
//}
//Node* Queue::iterate(Node* it)
//{
//	static Node* head=NULL;
//	head=(it!=NULL)?it:head;
//	if(head==NULL)
//		return NULL;
//	Node* temp=head;
//	head=head->next;
//	return temp;
//
//
//}

//Node* Queue::deQueue()
//{
//	if(!mHead) //empty check
//		return NULL;
//
//	Node* temp=mHead;
//	mHead=mHead->next;
//	mNumberOfElements--;
//	return mHead;
//
//}

Queue* initialiseQueue()
{
	Queue* newQ=(Queue*)malloc(sizeof(Queue));
	newQ->mHead=NULL;
	newQ->mTail=NULL;
	newQ->mNumberOfElements=0;
	return newQ;
}
void enQueue(Queue* q,void* data)
{
	Node *newNode=(Node*)malloc(sizeof(Node));
	newNode->data=data;
	newNode->next=NULL;
	if(q->mTail) //starting condition
		q->mTail->next=newNode;
	q->mTail=newNode;
	q->mNumberOfElements++;
	if(!q->mHead) //starting condition
		q->mHead=q->mTail;
}
Node* deQueue(Queue* q)
{
	if(!q->mHead) //empty check
		return NULL;

	Node* temp=q->mHead;
	q->mHead=q->mHead->next;
	q->mNumberOfElements--;
	if(!q->mHead)
		q->mTail=NULL;
	return temp;
}
bool isEmpty(Queue*q)
{
	return (q->mHead!=NULL);
}
