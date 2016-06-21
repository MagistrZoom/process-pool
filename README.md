# Dynamic process pool in C

This is my study repository for six programming work in dispicpline "System software". 

There you can see my implementation of multithreading server written in C with usage of Solaris libthread (just for interprocess mutexes)

## Pool logic

After start server creates MIN_WORKERS processes to handle user connections. After that it is starting to listen incomming port to delegate connections to worker through the Unix socket filedescriptor passing (SCM_RIGHTS).
Also it manages pool (if there is no free processes to handle connection server just start new one).  
In the end of handling of connection by worker it checks for amount of free processes and if free + 1 (himself) > MAX_WORKERS it makes a decision to do suicide



## Building

To build this project put 
     $ make
in CLI inside project root. You must use GNU make (on Solaris gmake)
  
  
## Usage
To run server on host 'hostname' put 
     $ server hostname port

To run client
     $ client hostname port dir [dir...]

