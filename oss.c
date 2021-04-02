#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>
#include "sharedMem.h"
#include "queue.h"

void startScheduling();
void ossExit(int sig);
void cleanUp();

int shmid;
int msqid;
struct PCB *controlTable;
FILE* fp;//File pointer for output log
struct sharedRes *shared;
//struct messageBuff msg; 
int toChild;
int toOSS;
const int maxTimeBetweenNewProcSec = 1; 
const int maxTimeBetweenNewProcNS = 500;
const int realTime = 15;//Chance of proccess to be a real time process
int n;//Store max number of children
int takenPids[18];
	//time data
	struct times exec = {0,0};//Holds time until next exec
	struct times totalCpuTime;
	struct times totalBlocked;
	struct times waitTime;
	struct times idleTime;
struct{
	long mtype;
	char message[100];
}msg;


int main(int argc, char *argv[]){
	//Setting up signals
	signal(SIGALRM, ossExit);
	alarm(3);
	signal(SIGINT, ossExit);
	//Get command line arguments	
	int c;	
	while((c=getopt(argc, argv, "n:s:h"))!= EOF){
		switch(c){
			case 'h':
				printf("\n-n: Total number of processes in system at any given time(Maximum of 18)");
				exit(0);
				break;
			case 'n':
				n = atoi(optarg);
				if(n > 18){
					n = 18;//Set n to 18 to 18 if n exceeds that amount or is left at -0
				}
				break;
		}
	}
	printf("Starting to schedule!\n");
	printf("Generating osslog.txt now...\n");
	//If n is 0 we'll default to 18
	if(n == 0){
		n = 18;
	}
	//OSSlog.txt will hold the logs for all the processes
	fp = fopen("osslog.txt","w");
	if(fp == NULL){
		perror("./oss: Error opening log file\n");
		exit(1);
	}

	//Attach PCB and clock to shared mem
	key_t key;
	key = ftok(".",'a');
	if((shmid = shmget(key,sizeof(struct sharedRes), IPC_CREAT | 0666)) == -1){
		perror("shmget");
		exit(1);	
	}
	
	shared = (struct sharedRes*)shmat(shmid,(void*)0,0);
	if(shared == (void*)-1){
		perror("Error on attaching memory");
	}

	//Attach queues for communication between processes
	key_t msgkey;
	if((msgkey = ftok("msgQueue1",925)) == -1){
		perror("ftok");
	}

	if((toChild = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}	
	
	if((msgkey = ftok("msgQueue2",825)) == -1){
		perror("ftok");
	}

	if((toOSS = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}
	
	int i;
	for(i = 0; i < 18; i++){
		takenPids[i] = 0;
	}	
	startScheduling();
	cleanUp();
	return 0;
}
void ossExit(int sig){
	switch(sig){
		case SIGALRM:
			printf("\n3 minutes has passed. The program will now terminate.\n");
			break;
		case SIGINT:
			printf("\nctrl+c has been registered. Now exitiing.\n");
			break;
	}
	printf("All process terminated succefully. Here's a look at the runtime report:\n");
	printf("Total time of system: %d:%d\n", shared->time.seconds, shared->time.nanoseconds);
	printf("Total idle time with no running processes: %d:%d\n", idleTime.seconds,idleTime.nanoseconds);
	
	int temp = totalBlocked.seconds * 1000000000 + totalBlocked.nanoseconds;
	printf("Average block time of the processes %u nanoseconds\n", temp/100);
	fprintf(fp,"Average block time of the processes %u nanosecodns\n", temp/100);	
	
	temp = totalCpuTime.seconds * 1000000000 + totalCpuTime.nanoseconds;
	printf("Average cpu time: %u nanoseconds\n", temp /100);
	fprintf(fp,"Average cpu time: %u nanoseconds\n", temp/100);

	temp = waitTime.seconds * 1000000000 + waitTime.nanoseconds;
	printf("Average wait of the processes %u nanoseconds\n", temp / 100);
	fprintf(fp,"Average wait of the processes %u nanoseconds\n", temp /100);
	cleanUp();
	kill(0,SIGKILL);
}

void cleanUp(){
	fclose(fp);
	msgctl(toOSS,IPC_RMID,NULL);
	msgctl(toChild,IPC_RMID,NULL);
	shmdt((void*)shared);	
	shmctl(shmid, IPC_RMID, NULL);
}

void addClock(struct times* time, int sec, int ns){
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= 1000000000){
		time->nanoseconds -=1000000000;
		time->seconds++;
	}
}


int firstEmptyBlock(){
	int i;
	int allTaken = 1;//Assume true

	/*for(i = 0; i < 18; i++){
		if(takenPids[i] != 1){
			allTaken = 0;//If an element doesn't equal 1 then all arent taken
		}
	}

	if(allTaken == 1){
		for(i = 0; i < 18; i++){
			takenPids[i] = 0;//reset pids
		}
	}*/

	for(i = 0; i < 18; i++){
		if(takenPids[i] == 0){
			takenPids[i] = 1;
			return i;
		}
	}
	return -1;
}

void startScheduling(){
	srand(time(0));//For random number generation

	pid_t child;//Holds temporary child

	int running = 0;
	//Create queues for storing local pids
	struct Queue* queue0 = createQueue(n);
	struct Queue* queue1 = createQueue(n);
	struct Queue* queue2 = createQueue(n);
	struct Queue* queue3 = createQueue(n);
	struct Queue* blockedQueue = createQueue(n);

	//Set vars for time quantum on each queue
	int baseTimeQuantum = 10;
	int cost0 = baseTimeQuantum  * 1000000;//Convert from ms to ns
	int cost1 = baseTimeQuantum * 2 * 1000000;
	int cost2 = baseTimeQuantum * 3 * 1000000;
	int cost3 = baseTimeQuantum * 4 * 1000000; 
	int active = 0, count, status, currentActive = 0,maxExecs = 100;

	int lines = 0;//keep track of # of lines in output log
	

	while(count < 100){//This count variable is how many processes have terminated
		addClock(&(shared->time), 0, 100000);
		if(running == 0){//Scheduler running with no ready processes(need to print this at the end)
			addClock(&(idleTime),0,100000);
		}
		//if max number of execs hasn't been met, active is less than n, and its time for another exec
		if( maxExecs > 0 &&(active < n) && ((shared->time.seconds > exec.seconds) ||(shared->time.seconds == exec.seconds && shared->time.nanoseconds >= exec.nanoseconds) )){
			//Random time until next exec	
			exec.seconds = shared->time.seconds;
			exec.nanoseconds = shared->time.nanoseconds;
			int secs = (rand() % (maxTimeBetweenNewProcSec + 1));
			int ns = (rand() % (maxTimeBetweenNewProcNS + 1));
			addClock(&exec,secs,ns);
			
			int newProc = firstEmptyBlock();
			if(newProc > -1){
			//	printf("New process:%d\n",newProc);
				//Only do fork/exec if we find an open processblock
				if((child = fork()) == 0){
					char str[10];
					sprintf(str, "%d", newProc);
					execl("./user",str,NULL);
					exit(0);
				}
				active++;
				maxExecs--;
				//set control block parameters
				shared->controlTable[newProc].pid = child;
				shared->controlTable[newProc].processClass = ((rand()% 100) < realTime) ? 1 : 0;//If 0 then user process
				shared->controlTable[newProc].totalTimeInSystem.seconds = shared->time.seconds;
				shared->controlTable[newProc].totalTimeInSystem.nanoseconds = shared->time.nanoseconds;	
				shared->controlTable[newProc].lpid = newProc;
				shared->controlTable[newProc].totalCpuTime.nanoseconds = 0;
				shared->controlTable[newProc].totalCpuTime.seconds = 0;
				shared->controlTable[newProc].blockedTime.nanoseconds = 0;
				shared->controlTable[newProc].blockedTime.seconds = 0;
				if(shared->controlTable[newProc].processClass == 0){//IF user process start it at queue1
					shared->controlTable[newProc].priority = 1;
					enqueue(queue1, shared->controlTable[newProc].lpid);
					if(lines < 10000){
						fprintf(fp,"OSS: Generating process with PID %d and putting it in queue %d at time %d:%u\n",shared->controlTable[newProc].lpid, 1, shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
				}
				if(shared->controlTable[newProc].processClass == 1){//If realtime process start  it at queue0
					shared->controlTable[newProc].priority = 0;
					enqueue(queue0, shared->controlTable[newProc].lpid);
					if(lines < 10000){
						fprintf(fp,"OSS: Generating process with PID %d and putting it in queue %d at time %d:%u\n",shared->controlTable[newProc].lpid,0, shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
				}
			}
		}
		

			//start running if we have procceses and one isnt currently running
		if((isEmpty(queue0) == 0 || isEmpty(queue1) == 0 || isEmpty(queue2) == 0 || isEmpty(queue3) == 0) && running == 0){	
			//Check queues from highest priority to lowest
			int queue;
			strcpy(msg.message,"");
			if(isEmpty(queue0) == 0){//Remove from queue0
				queue = 0;
				currentActive = dequeue(queue0);//Get the newly active processes simulated id
				msg.mtype = shared->controlTable[currentActive].pid;//Make the mytype by the actual pid
				msgsnd(toChild, &msg, sizeof(msg), 0);//Wake up child
			}
			else if(isEmpty(queue1) == 0){//If not then queue0 then queue1
				currentActive = dequeue(queue1);
				queue = 1;
				msg.mtype = shared->controlTable[currentActive].pid;//Make the mytype by the actual pid
				msgsnd(toChild, &msg, sizeof(msg), 0);
			}
			else if(isEmpty(queue2) == 0){//If not from queue1 then queue2
				currentActive = dequeue(queue2);
				queue = 2;
				msg.mtype = shared->controlTable[currentActive].pid;//Make the mytype by the actual pid
				msgsnd(toChild, &msg, sizeof(msg),0);
			}
			else if(isEmpty(queue3) == 0){//If not from queue2 then queue3
				currentActive = dequeue(queue3);
				queue = 3;
				msg.mtype = shared->controlTable[currentActive].pid;//Make the mytype by the actual pid
				msgsnd(toChild, &msg, sizeof(msg), 0);
			}
			if(lines < 10000){
				fprintf(fp,"OSS: Dispatching process with PID %d from queue %d at time %d:%d \n", currentActive, queue, shared->time.seconds, shared->time.nanoseconds);		
				lines++;
			}
			//Account for scheduling time in system clock
			int timeCost = ((rand()% 100) + 50);
			addClock(&(shared->time), 0, timeCost);	
			running = 1;	
		}
		
		if(isEmpty(blockedQueue) == 0){
			int k;
			for(k = 0; k < blockedQueue->size; k++){
				int lpid = dequeue(blockedQueue);//We'll user the lpid to index the process control table
				//If we recieved the finish message we can remove from the blocked queue and schedule
				if((msgrcv(toOSS, &msg, sizeof(msg),shared->controlTable[lpid].pid,IPC_NOWAIT) > -1) && strcmp(msg.message,"FINISHED") == 0){
					if(shared->controlTable[lpid].processClass == 0){
						enqueue(queue1, shared->controlTable[lpid].lpid);
						shared->controlTable[lpid].priority = 1;
					}
					else if(shared->controlTable[lpid].processClass == 1){
						enqueue(queue0, shared->controlTable[lpid].lpid);
						shared->controlTable[lpid].priority = 0;
					}
					if(lines < 10000){
						fprintf(fp,"OSS: Unblocking process with PID %d at time %d:%d\n",lpid,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
				}
				else{
					enqueue(blockedQueue, lpid);
				}
				
			}
		}
		//Sends message to children if running
		if(running == 1){
			running = 0;//set running back to 0	
			//Block while waiting for response from child
			if((msgrcv(toOSS, &msg, sizeof(msg),shared->controlTable[currentActive].pid, 0)) > -1 ){
				//If child dies
				if(strcmp(msg.message,"DIED") == 0){
					while(waitpid(shared->controlTable[currentActive].pid, NULL, 0) > 0);
					msgrcv(toOSS,&msg, sizeof(msg), shared->controlTable[currentActive].pid,0);
					//set totaltime in system
					shared->controlTable[currentActive].totalTimeInSystem = shared->time;
					//set total cpu time
					//shared->controlTable[currentActive].totalCpuTime.seconds = shared->time.seconds - shared->controlTable[currentActive].totalCpuTime.seconds;
					shared->controlTable[currentActive].totalCpuTime.nanoseconds = shared->time.nanoseconds - shared->controlTable[currentActive].totalCpuTime.nanoseconds; 
					//Get percentage of time used as integer
					int p = atoi(msg.message);
					int time;	
					if(shared->controlTable[currentActive].priority == 0){
						time = cost0 * (p / 100);
					}
					else if(shared->controlTable[currentActive].priority == 1){
						time = cost1 * (p / 100);
					}
					else if(shared->controlTable[currentActive].priority == 2){
						time = cost2 * (p / 100);
					}	
					else if(shared->controlTable[currentActive].priority == 3){
						time = cost3 * (p / 100);
					}
					active--;
					count++;
					//printf("Terminating process: %d\n", currentActive);
					addClock(&(shared->time),0,time);//add ns to account for time cost	
					addClock(&(shared->controlTable[currentActive].totalCpuTime),0,time);	
	
					takenPids[currentActive] = 0;				
					if(lines < 10000){
						fprintf(fp,"OSS: Terminating process with PID %d after %u nanoseconds\n",shared->controlTable[currentActive].lpid, time);
						lines++;
					}
					addClock(&totalCpuTime,shared->controlTable[currentActive].totalCpuTime.seconds,shared->controlTable[currentActive].totalCpuTime.nanoseconds);
					
					addClock(&totalBlocked, shared->controlTable[currentActive].blockedTime.seconds, shared->controlTable[currentActive].blockedTime.nanoseconds);
					//Wait time = totalTimeInSystem - blockedTime - totalCpuTIme
					int waitSec = shared->controlTable[currentActive].totalTimeInSystem.seconds;
					int waitNS = shared->controlTable[currentActive].totalTimeInSystem.nanoseconds;
					
					waitSec -= shared->controlTable[currentActive].totalCpuTime.seconds;
					waitNS -= shared->controlTable[currentActive].totalCpuTime.nanoseconds;
					
					waitSec -= shared->controlTable[currentActive].blockedTime.seconds;
					waitNS -= shared->controlTable[currentActive].blockedTime.nanoseconds;
					addClock(&waitTime,waitSec,waitNS);
				}
				//If child uses all its time
				if(strcmp(msg.message,"USED_ALL_TIME") == 0){
					int prevPriority;
					if(shared->controlTable[currentActive].priority == 0){
						prevPriority = shared->controlTable[currentActive].priority;
						shared->controlTable[currentActive].priority = 0;//If at this priority then process stays in queue 0 until termination
						enqueue(queue0, shared->controlTable[currentActive].lpid);
						addClock(&(shared->time) ,0 ,cost0);
						addClock(&(shared->controlTable[currentActive].totalCpuTime),0 ,cost0);
					}
					else if(shared->controlTable[currentActive].priority == 1){
						prevPriority = shared->controlTable[currentActive].priority;
						shared->controlTable[currentActive].priority = 2;//move to lower priority
						enqueue(queue2, shared->controlTable[currentActive].lpid);
						addClock(&(shared->time), 0,cost1);
						addClock(&(shared->controlTable[currentActive].totalCpuTime),0, cost1);
					}
					else if(shared->controlTable[currentActive].priority == 2){
						prevPriority = shared->controlTable[currentActive].priority;
						shared->controlTable[currentActive].priority = 3;//move to lower priority
						enqueue(queue3, shared->controlTable[currentActive].lpid);
						addClock(&(shared->time) ,0, cost2);
						addClock(&(shared->controlTable[currentActive].totalCpuTime),0 ,cost2);
					}
					else if(shared->controlTable[currentActive].priority == 3){
						prevPriority = shared->controlTable[currentActive].priority;
						shared->controlTable[currentActive].priority = 3;//stays at lowest priority
						enqueue(queue3, shared->controlTable[currentActive].lpid);
						addClock(&(shared->time) ,0 ,cost3);
						addClock(&(shared->controlTable[currentActive].totalCpuTime),0, cost3);
					}
					if(lines < 10000){
						fprintf(fp,"OSS: Moving process with pid %d from queue %d to queue %d\n", shared->controlTable[currentActive].lpid, prevPriority, shared->controlTable[currentActive].priority);
						lines++;
					}
				}

				//If only some time is used
				if(strcmp(msg.message,"USED_SOME_TIME") == 0){
					msgrcv(toOSS, &msg, sizeof(msg),shared->controlTable[currentActive].pid,0);
					//Get percentage of time used as integer
					int p = atoi(msg.message);
					int time;	
					if(shared->controlTable[currentActive].priority == 0){
						time = cost0  / p;//Cost of queue takes  p of assigned quantum
					}
					else if(shared->controlTable[currentActive].priority == 1){
						time = cost1  / p;
					}
					else if(shared->controlTable[currentActive].priority == 2){
						time = cost2 / p;
					}	
					else if(shared->controlTable[currentActive].priority == 3){
						time = cost3  / p;
					}
					addClock(&(shared->time),0,time);//add ns to account for time cost	
					addClock(&(shared->controlTable[currentActive].totalCpuTime),0,time);	
					if(lines < 10000){
						fprintf(fp,"OSS: Process with PID %d has been preempted after %d nanoseconds\n",shared->controlTable[currentActive].lpid, time);
						lines++;
					}
				//Create a blocked queue and insert pids here
					enqueue(blockedQueue,shared->controlTable[currentActive].lpid);
				}

			}
		}


	}
	pid_t wpid;//wait for any children that haven't quite finished yet
	while((wpid = wait(&status)) > 0);	
	
	//Print finishing information
	fprintf(fp,"Hey, you made it! Here's a quick stat report: \n");
	printf("All process terminated succefully. Here's a look at the runtime report:\n");
	printf("Total time of system: %d:%d\n", shared->time.seconds, shared->time.nanoseconds);
	printf("Total idle time with no running processes: %d:%d\n", idleTime.seconds,idleTime.nanoseconds);
	
	int temp = totalBlocked.seconds * 1000000000 + totalBlocked.nanoseconds;
	printf("Average block time of the processes %u nanoseconds\n", temp/100);
	fprintf(fp,"Average block time of the processes %u nanosecodns\n", temp/100);	
	
	temp = totalCpuTime.seconds * 1000000000 + totalCpuTime.nanoseconds;
	printf("Average cpu time: %u nanoseconds\n", temp /100);
	fprintf(fp,"Average cpu time: %u nanoseconds\n", temp/100);

	temp = waitTime.seconds * 1000000000 + waitTime.nanoseconds;
	printf("Average wait of the processes %u nanoseconds\n", temp / 100);
	fprintf(fp,"Average wait of the processes %u nanoseconds\n", temp /100);

}
