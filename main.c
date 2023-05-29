#include "unboundedqueue.h"
#include "util.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAX_LENGTH_PATH 255

int main(int argc, char** argv){

    if (argc != 3){
        fprintf (stdout, "Numeri di parametri non corretto\n");
        exit(EXIT_FAILURE); 
    }

    
    pid_t pid; 
    SYSCALL_EXIT (fork, pid, fork(), " sulla creazione del processo Master"); 
    if (pid == 0){
        execv ("./m.out", argv);     
        perror ("cannot exec"); 
        exit(EXIT_FAILURE);  
    }

    wait(NULL); //rilascio le risorse del master
    return 0; 
    }