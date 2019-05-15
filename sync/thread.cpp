#include "thread.hpp"
#include <map>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <semaphore.h>
using namespace std;

#define INTERVAL 50
#define MAX 128
#define SEMA_MAX 65536

//locking
static sigset_t set;
static int lock_counter=0;
void lock(){
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	int p = sigprocmask(SIG_BLOCK, &set, NULL);
	if(p!=0){
		perror("Error calling pthread_sigmask()");
                exit(1);
	}
	lock_counter++;
	if(lock_counter>1){
		perror("Need to call unlock \n");
		lock_counter=1;
	}
	return;
}
void unlock(){
	lock_counter--;
	if(lock_counter<0){
		perror("Need to call lock before unlock");
		lock_counter=0;
	}
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}


//TCB
static thread main_thread;
static map<pthread_t, thread> thread_pool;
static int counter=0;
static map<pthread_t, thread>::iterator curr;

static map<int, semaphore> sema_pool;
static int sema_counter=0;

int block_state(){
	int i=0;
	 for(map<pthread_t,thread>::iterator it=thread_pool.begin();it!=thread_pool.end();it++){
	 	if(it->second.status==T_BLOCK){
			i++;
		}
	 
	 }
	 return i;
}

void thread_switch(int signo){
	//cout<<"call switch\n";
	//cout<<"curr id: "<<curr->second.id<<endl;
	if(block_state()==0){
		//cout<<"can not be switched just return \n";	
		return;
	}
	int c=setjmp(curr->second.buffer);
	//cout<<"value of setjmp is "<<c<<endl;
	if(c==0){
		if(curr->second.status == T_RUNNING){
			curr->second.status = T_BLOCK;
		}
	
		if((++curr) == thread_pool.end()){
			for(map<pthread_t,thread>::iterator it=thread_pool.begin();it!=thread_pool.end();it++){
                                if(it->second.status==T_BLOCK){
                                        curr=it;
                                        break;
                                }	
			}
		}

		else{
			for(map<pthread_t,thread>::iterator it=curr++;it!=thread_pool.end();it++){
				if(it->second.status==T_BLOCK){
					curr=it;
					break;
				}
				map<pthread_t,thread>::iterator temp=it;
				if(++temp==thread_pool.end()){
                        		for(map<pthread_t,thread>::iterator it=thread_pool.begin();it!=thread_pool.end();it++){
                                		if(it->second.status==T_BLOCK){
                                        		curr=it;
                                        		break;
                               			}
                        		}
					break;
				}
			}
		}
		curr->second.status=T_RUNNING;
		//cout<<"curr id before long jump "<<curr->second.id<<endl;
		longjmp(curr->second.buffer, 1);
	}
	//cout<<"not 0 XD";
	return;

}

void thread_switch(map<pthread_t, thread>::iterator t){
        int c=setjmp(curr->second.buffer);
        if(c==0){
		curr->second.status = T_WAIT;
               
                curr=t;      
                curr->second.status=T_RUNNING;
                longjmp(curr->second.buffer, 1);
        }
        return;

}


static struct itimerval iterval={0};
static struct itimerval current_timer={0};
static struct itimerval zero_timer={0};
static struct sigaction action;
void scheduler(){
	action.sa_handler = thread_switch;
	action.sa_flags =  SA_NODEFER;

 	if (sigaction(SIGALRM, &action, NULL) == -1) {
    		perror("Error calling sigaction()");
    		exit(1);
 	}
	//convert to ms
  	iterval.it_value.tv_sec =     INTERVAL/1000;
  	iterval.it_value.tv_usec =    (INTERVAL*1000) % 1000000;
  	iterval.it_interval = iterval.it_value;

}

static long int i64_ptr_mangle(long int p)
{
	long int ret;
	asm(" mov %1, %%rax;\n"
	" xor %%fs:0x30, %%rax;"
	" rol $0x11, %%rax;"
	" mov %%rax, %0;"
	: "=r"(ret)
        : "r"(p)
        : "%rax"
        );
        return ret;
}



void start(){
	scheduler();
	main_thread.id=0;
	main_thread.parent=-1;
	main_thread.child=0;
	main_thread.stack=NULL;
	main_thread.status=T_RUNNING;
	main_thread.arg=NULL;
	main_thread.start_routine=NULL;
	thread_pool.emplace(main_thread.id, main_thread);
	curr=thread_pool.find(main_thread.id);	
	setjmp(main_thread.buffer);
	//start timer
	current_timer = iterval;
        if(setitimer(ITIMER_REAL,&current_timer,NULL)==-1){
                perror("error calling setitimer()");
                exit(1);
        }
	
}

void wrapper_func(){
      	pthread_exit( curr->second.start_routine(curr->second.arg) );
}

int pthread_create(pthread_t *thread, 
		const pthread_attr_t *attr, 
		void *(*start_routine)(void*), 
		void *arg){
	if(counter==0){
		counter+=1;
		start();
	}

	//pause
	//setitimer(ITIMER_REAL,&zero_timer,&current_timer);
	lock();
	struct thread new_thread;
	new_thread.id=counter;
	new_thread.parent=-1;
	*thread=new_thread.id;
	counter+=1;
	new_thread.join=0;
	new_thread.status=T_BLOCK;
	new_thread.start_routine = start_routine;
	new_thread.arg = arg;
	new_thread.stack=(unsigned long*)malloc(stack_space);
	setjmp(new_thread.buffer);

	void (*pc)() = &wrapper_func;	
	new_thread.buffer[0].__jmpbuf[6] = i64_ptr_mangle((unsigned long)(new_thread.stack+ 32767/8 -2));
	new_thread.buffer[0].__jmpbuf[7] = i64_ptr_mangle((unsigned long)pc);

	thread_pool.emplace(new_thread.id, new_thread);
	//resume
	//setitimer(ITIMER_REAL,&current_timer,NULL);
	unlock();	
	return 0;
}
int pthread_join(pthread_t th, void **value_ptr){
	//cout<<"in pthread join"<<endl;
	lock();
	if(th==curr->second.id){
		unlock();
		return EDEADLK;
	}
	map<pthread_t, thread>::iterator target;
	target=thread_pool.find(th);
	if(target==thread_pool.end()){
		perror("No such thread is found");
		unlock();
		return ESRCH;
	}
	
	if(target->second.join==1){
		unlock();
		return EINVAL;
	}
	target->second.join=1;
	target->second.parent=curr->second.id;
	curr->second.child+=1;
	unlock();

	//successfully find the thread
	if(target->second.status==T_EXIT){
		value_ptr=&target->second.return_val;
	}
	else{
		//cout<<"in thread join and switching to new thread"<<endl;
		thread_switch(target);
		
		//cout<<"back from thread_switch"<<endl;
		target=thread_pool.find(th);
		
		//cout<<"target id: "<<target->second.id<<" "<<target->second.status<<endl;
		if(target->second.return_val==NULL){
			//cout<<"it is NULL \n";
		}
		else{
			*value_ptr=target->second.return_val;
			//cout<<"value of the ptr"<<value_ptr<<endl;
		}
	}	
	thread_pool.erase(target->second.id);
	return 0;
}
void pthread_exit(void *value_ptr){
	//setitimer(ITIMER_REAL,&zero_timer,&current_timer);
	lock();
	//cout<<"in exit id is "<<curr->second.id<<endl;
	curr->second.return_val=value_ptr;
	
	if(curr->second.id==0){
		//cout<<"main exit";
		setitimer(ITIMER_REAL, &zero_timer, NULL);
		curr->second.status=T_EXIT;
		for(map<pthread_t,thread>::iterator it=thread_pool.begin();it!=thread_pool.end();it++){
			if(it->second.stack!=NULL){
				free(it->second.stack);
				it->second.stack=NULL;
			}
		}
		exit(0);
	}
	curr->second.status=T_EXIT;
	//cout<<"in exit, curr id; "<<curr->second.id<<endl;
	//cout<<"curr parent id is:"<<curr->second.parent<<endl;	
	if(curr->second.parent!=-1){
		if(curr->second.stack!=NULL){
                        free(curr->second.stack);
                        curr->second.stack=NULL;
                }
		pthread_t tid=curr->second.parent;
		curr->second.parent=-1;
		curr=thread_pool.find(tid);
		curr->second.child-=-1;
		if(curr->second.child==0){
			curr->second.status=T_BLOCK;
		}
                for(map<pthread_t,thread>::iterator it=curr;it!=thread_pool.end();it++){
			if(it->second.status==T_BLOCK){
				curr=it;
				break;
			}
		
                        map<pthread_t,thread>::iterator temp=it;
                        if(++temp==thread_pool.end()){
				for(map<pthread_t,thread>::iterator it=thread_pool.begin();it!=thread_pool.end();it++){
                        		if(it->second.status==T_BLOCK){
                                		curr=it;
                                		break;
                        		}
				}
				break;
                        }
		}
		
		
	}
	else{
	
		if(curr->second.stack!=NULL){
			free(curr->second.stack);
			curr->second.stack=NULL;
		}
		curr->second.status=T_EXIT;
        	if((++curr) == thread_pool.end()){
			curr=thread_pool.find(0);
        	}
                
		else{
			for(map<pthread_t,thread>::iterator it=curr++;it!=thread_pool.end();it++){
				//cout<<"In exit searching for T_BLOCK: "<<it->second.id<<endl;	
				if(it->second.status==T_BLOCK){
					curr=it;
					break;
				}
				map<pthread_t,thread>::iterator temp=it;
				if(++temp==thread_pool.end()){
					curr=thread_pool.find(0);
					break;
				}
			}
		}
	}
	//cout<<"in exit we are now going to: "<<curr->second.id<<endl;
	curr->second.status=T_RUNNING;
	//setitimer(ITIMER_REAL,&current_timer,NULL);
	unlock();
	longjmp(curr->second.buffer, 1);	

}

pthread_t pthread_self(void){
	return curr->second.id;
}
     
//semaphores
//
      
static map<int, semaphore>::iterator it;

int sem_init(sem_t *sem, int pshared, unsigned int value){
	//cout<<"int sema init"<<endl;
	lock();
	if(pshared!=0){
		perror("pshared should be 0");
		unlock();
		return ENOSYS;
	}
	semaphore s;
	s.id=sema_counter;
	//cout<<"s.id is: "<<s.id;
	s.initialized=1;
	
	sema_counter++;
	if(value > SEMA_MAX){
		perror("semaphore value too large");
		unlock();
		return EINVAL;
	}
	s.val=value;		
	sema_pool.emplace(s.id, s);
	sem->__align=s.id;

	unlock();
	return 0;
}

int sem_destroy(sem_t *sem){
	//cout<<"in sem desotry"<<endl;
	lock();
	int target_id=sem->__align;
	it = sema_pool.find(target_id);
	if(it==sema_pool.end()){
		perror("semaphore not found");
		unlock();
		return EINVAL;
	}
	if(it->second.initialized!=1){
		perror("Is not initialized and thus can not be destoried");
		unlock();
		return EINVAL;
	}
	sema_pool.erase(target_id);
	unlock();
	return 0;

}
int sem_wait(sem_t *sem){
	
	//cout<<"in wait"<<endl;
	lock();
	int target_id=sem->__align;
	//cout<<"sema target id is: "<<target_id<<endl;
	
        it = sema_pool.find(target_id);
	
	if(sema_pool.count(target_id)==0){
                perror("in wait semaphore not found");
                unlock();
		return EINVAL;
        }

	if(it->second.val>0){
		it->second.val-=1;
		unlock();
		return 0;
	}
	curr->second.status=T_WAIT;
	it->second.waitlist.push(curr->second.id);
	unlock();
	thread_switch(1);
	return 0;
}
int sem_post(sem_t *sem){
	//cout<<"in post"<<endl;
	lock();
	int target_id=sem->__align;
        it = sema_pool.find(target_id);
        if(sema_pool.count(target_id) == 0){
                perror("semaphore not found");
                unlock();
		return EINVAL;
        }

	if(it->second.val>0){
		it->second.val++;
		if(it->second.val>SEMA_MAX){
			perror("val too large");
			unlock();
			return EOVERFLOW;
		}
		return 0;
	}

	if(it->second.waitlist.empty()!=true){
		//staff inside
		pthread_t target_id=it->second.waitlist.front();
		it->second.waitlist.pop();
		map<pthread_t,thread>::iterator target=thread_pool.find(target_id);
		target->second.status=T_BLOCK;
		unlock();
		return 0;
	}
	
	it->second.val++;
	unlock();
	return 0;
}
















	
