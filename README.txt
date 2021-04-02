Read 

This project will involve the main executable alternating execution with user processes much as the dispatcher might in an OS. ou will start the operating system simulator (call the executable oss) as one main process who will fork multiple children at random times. we are limiting ourselves to 20 processes in this class, you should allocate space for up to 18 process control blocks. Your process table should contain a method to keep track of what process control blocks are currently in use. oss simulates time passing in the system by adding time to the clock and it is the only process that should modify the clock.

oss [-h] [-s t] [-l f]

-h Describe how the project should be run and then, terminate. 
-s t Indicate how many maximum seconds before the system terminates 
-l f Specify a particular name for the log file


EXECUTION: 
make
./oss
The generated log file is osslog.txt
-h prints help
-n limits active number of processes
