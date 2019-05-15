#ifndef thread_h
#define thread_h

#include <pthread.h>
#include <iostream>
#include <setjmp.h>
#include <string>
#include <semaphore.h>
#include <queue>
const int stack_space=32767;

enum Token{
	T_RUNNING,
	T_BLOCK,
	T_EXIT,
	T_WAIT,

};
struct thread{
	bool join;
	int child;
	pthread_t parent;
	pthread_t id;
	unsigned long* stack;
	jmp_buf buffer;
	Token status;
	void *(*start_routine)(void*);
	void *arg;
	void *return_val;
};


int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg);
void pthread_exit(void *value_ptr);
pthread_t pthread_self(void);

int pthread_join(pthread_t thread, void **value_ptr);

//semaphore struct
//
struct semaphore{
	Token status;
	bool initialized;
	int id;
	unsigned int val;
	std::queue<pthread_t> waitlist;	
};
#endif
