
#ifndef SHAREDMEM_H_
#define SHAREDMEM_H_

struct times{
	int nanoseconds;
	int seconds;
};
struct PCB{
	struct times totalCpuTime;
	struct times  totalTimeInSystem;
	struct times timeLastBurst;
	struct times blockedTime;
	int processClass; //realtime or user class
	int pid;//actual pid of process
	int lpid;//simulate:wq
	int priority;
};



struct sharedRes{
	struct PCB controlTable[18];
	struct times time;
};
#endif
