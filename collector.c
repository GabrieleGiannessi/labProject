#include "unboundedqueue.h"
#include "util.h"
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
    int* connections;
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
     

    char buf[N]; 
    while (1){
        int* fd = (int*) pop(q); //puntatore al socket usato dal server per comunicare con un certo client
        if (fd == NULL){ 
            break;
        } 
        
       int n = read (*fd, buf, N); //leggo il messaggio dal client

       if (n > 0){ 
       printf ("%s", buf); 
       write (*fd, buf, strlen(buf) + 1);
        }
        if (n==0){ //il cliente è uscito
            //il server chiude la connessione
            pthread_mutex_lock(mtx); 
            close(*fd); //chiudo il socket 
            FD_CLR(*fd,clients);//aggiorno la maschera
    
            if (*fd == *fdMax) aggiornaMax(*clients, fdMax); 
            pthread_mutex_unlock(mtx);
            free(fd);
            // printf ("%d\n", *(((arg_t*)args)->connections)); 
            *((arg_t*)args)->connections += 1; 
            break; 
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

    printf ("n\tavg\tdev\tfile\n");
    printf ("-----------------------------------------------------------\n");

    int server = socket (AF_INET, SOCK_STREAM,0); //restituisce un fd
    struct sockaddr_in serverAddr;
    serverAddr.sin_family=AF_INET; 
    serverAddr.sin_port=htons(1111);
    serverAddr.sin_addr.s_addr=inet_addr("172.27.68.197");

    int r1,r2;
    SYSCALL_EXIT(bind,r1,bind (server,(struct sockaddr*)&serverAddr, sizeof(serverAddr)), " sulla bind\n"); 
    SYSCALL_EXIT(listen, r2,listen (server, MAX_NUM_FILE + 1), " sulla listen\n"); //mi metto in ascolto del numero massimo di file che possono esserci dentro la dir corrente

    fd_set allFDs, readFDs; //allFDs: tutti i client+server

    FD_ZERO(&allFDs); 
    FD_SET(server,&allFDs); 
    int fdMax = server;

    pthread_mutex_t* m = (pthread_mutex_t*) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init(m, NULL);   
    Queue_t* q = initQueue(); 
    int* connessioniEffettuate = (int*) malloc (sizeof (int)); 
    *connessioniEffettuate = 0; 
    arg_t* threadArgs = (arg_t*) malloc (sizeof (arg_t)); 
    threadArgs->q=q;
    threadArgs->mutex=m;
    threadArgs->fdMax=&fdMax;
    threadArgs->clients=&allFDs;
    threadArgs->connections = connessioniEffettuate; 
    
    
    int numThread = atoi(argv[2]);

    for (int i = 0; i < numThread; i++){
        int err; 
        pthread_t tid;     
        SYSCALL_EXIT(pthread_create,err,pthread_create(&tid, NULL, worker, threadArgs), " sulla creazione del %d thread server\n", i);
        pthread_detach(tid); //so che il server deve rimanere in attesa, così forzo il fatto che non possa fare la join sui thread
    }
    
    //questo è il thread che rappresenta il server: rimane in attesa fino a quando l'operazione di scrittura dell'output non è completata 
    while (*(threadArgs->connections) != numThread){
    pthread_mutex_lock (m); 
    readFDs = *(threadArgs->clients); //copia del set
    int currMax = *threadArgs->fdMax;
    
    pthread_mutex_unlock(m);
    int r; 
    SYSCALL_EXIT(select, r, select(currMax+1,&readFDs,NULL,NULL,NULL), "Facendo la Select\n"); 
    for (int i = 0; i < currMax + 1;i++){
        if (FD_ISSET(i, &readFDs)){
          
            if (i == server){ //server pronto
                int clientFD = accept (server, NULL, NULL); //ritorna il fd usato per la connessione con il client
                pthread_mutex_lock(m); 
                FD_SET( clientFD,threadArgs->clients); //metto il client in allFDs
                if(clientFD > fdMax) *threadArgs->fdMax = clientFD; //aggiorno fdMax in questo caso
                pthread_mutex_unlock(m);  
            }else {
                //read 
                int* client = (int*) malloc(sizeof (int)); 
                *client = i; 
                push(threadArgs->q,client);
                }
            }
        } 
    }
    
    int cl; 
    SYSCALL_EXIT(close, cl, close(server), " sulla chiusura del server");
    pthread_mutex_destroy(m); 
    deleteQueue(q); 
    return 0;
}