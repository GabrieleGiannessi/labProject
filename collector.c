#include "unboundedqueue.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <pthread.h>
#define N 1024
#define MAX_NUM_FILE 200

/**
 * paradigma master-worker con un pool di thread: quando arriva un client, faccio l'accept e creo un thread che lo gestisce
 * faccio detatch del thread (del server) perchè deve mantenere la connessione col client
 * Il server, con l'aiuto di thread usati per garantire maggior efficienza sulla connessione, prenderà l'output dagli workers
 *  * Il server accettando il client riceve il suo sfd 
* gli workers manderanno richieste continuamente stando sulla stessa connessione    
*/
typedef struct{
    Queue_t* q;
    fd_set* clients; 
    pthread_mutex_t* mutex; 
    int* fdMax;
}arg_t;

void aggiornaMax(fd_set set , int* max){
    while (!FD_ISSET(*max, &set)) *max = *max - 1;
}


void* worker(void* args){
    //devo dare la coda condivisa dove prendere i vari socket 
    Queue_t* q = ((arg_t*)args)->q;
    int* fdMax = ((arg_t*)args)->fdMax;
    fd_set* clients = ((arg_t*)args)->clients; 
    pthread_mutex_t* mtx = ((arg_t*)args)->mutex;
    pthread_mutex_init(mtx, NULL);

    char buf[N]; 
    while (1){ 
        int* fd = (int*) pop(q); //puntatore al socket
        if (fd == NULL){ 
            break;
        } 
        
       int n = read (*fd, buf, N); //leggo il messaggio dal client
       
       if (strcmp(buf, "end") == 0){ 
        break;
       } 

       if (n > 0){ 
       printf ("%s", buf); 
       //write (*fd, buf, strlen(buf) + 1);
        }

        if (n==0){ //il cliente è uscito
            //il server chiude la connessione
            pthread_mutex_lock(mtx); 
            FD_CLR(*fd,clients);
            aggiornaMax(*clients, fdMax);
            pthread_mutex_unlock(mtx);
            } 
        free(fd);   
    }
    return NULL; 
}

int main (int argc, char** argv){
    //devo settare i socket e fare le connessioni
    if (argc != 3){
        fprintf (stdout,"Numero di parametri sbagliato, riprova\n"); 
        exit (EXIT_FAILURE);
    }

    int server = socket (AF_INET, SOCK_STREAM,0); //restituisce un fd
    char buff[N];
    struct sockaddr_in serverAddr;
    serverAddr.sin_family=AF_INET; 
    serverAddr.sin_port=htons(1111);
    serverAddr.sin_addr.s_addr=inet_addr("172.27.68.197");

    bind (server,(struct sockaddr*)&serverAddr, sizeof(serverAddr)); 
    listen (server, MAX_NUM_FILE + 1); //mi metto in ascolto del numero massimo di file che possono esserci dentro la dir corrente

    fd_set allFDs, readFDs; //allFDs: tutti i client+server, 
    FD_ZERO(&allFDs); 
    FD_SET(server,&allFDs); 
    int fdMax = server;
    pthread_mutex_t* m = (pthread_mutex_t*) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init(m, NULL);   

    Queue_t* q = initQueue(); 

    arg_t threadArgs; 
    threadArgs.q=q;
    threadArgs.mutex=m;
    threadArgs.fdMax=&fdMax;
    threadArgs.clients=&allFDs;
    int numThread = atoi(argv[2]);

    for (int i = 0; i < numThread; i++){
        pthread_t tid; 
        pthread_create(&tid, NULL, worker, &threadArgs);
        pthread_detach(tid); //so che il server deve rimanere in attesa, così forzo il fatto che non possa fare la join sui thread
    }
    
    //questo è il thread dispatcher che rappresenta il server: rimane in attesa fino a quando l'operazione di scrittura dell'output non è completata
    while (1){
    pthread_mutex_lock (m); 
    readFDs = allFDs; //copia del set
    int currMax = fdMax; 
    pthread_mutex_unlock(m);
    select (currMax+1,&readFDs,NULL,NULL,NULL); 

    for (int i = 0; i < currMax + 1;i++){
        if (FD_ISSET(i, &readFDs)){
            if (i == server){ //server pronto
                int clientFD = accept (server, NULL, NULL); 
                pthread_mutex_lock(m); 
                FD_SET( clientFD,&allFDs); //metto il client in allFDs
                if(clientFD > fdMax) fdMax = clientFD; //aggiorno fdMax in questo caso
                pthread_mutex_unlock(m);  
            }else {
                //read 
                int* client = (int*) malloc (sizeof (int)); 
                *client = i; 
                push(q,client);
                }
            }
        }
    } 

    close (server); 
    return 0;
}
