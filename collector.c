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
    fd_set* closed;
    pthread_mutex_t* mutex; 
    int* fdMax;
    int* connections;
}arg_t;

void aggiornaMax(fd_set* set , int* max){
    while (!FD_ISSET(*max, set)) *max = *max - 1;
}

void deleteArgs(arg_t* arg){ 
    deleteQueue(arg->q); 
    free(arg->mutex); 
    free(arg->connections); 
    free(arg);
}

void* worker(void* args){
    //devo dare la coda condivisa dove prendere i vari socket 
    Queue_t* q = ((arg_t*)args)->q;
    int* fdMax = ((arg_t*)args)->fdMax;
    fd_set* clients = ((arg_t*)args)->clients; 
    fd_set* closed = ((arg_t*)args)->closed;
    pthread_mutex_t* mtx = ((arg_t*)args)->mutex;
    //int* conn = ((arg_t*)args)->connections;
     

    char buf[N];
    int* fd = (int*) pop(q);  //puntatore al socket usato dal server per comunicare con un certo client 
    
        if (fd == NULL){  
            return NULL; 
        }
    
    while (1){ 
       int n = read (*fd, buf, N); //leggo il messaggio dal client
       
       if (n > 0){ 
       printf ("%s", buf); 
       write (*fd, buf, strlen(buf) + 1);
        }

        if (n==0){ //il cliente è uscito
            //il server chiude la connessione
            pthread_mutex_lock(mtx); 
            FD_CLR(*fd,clients);//aggiorno la maschera 
            FD_SET(*fd,closed);//il fd è stato chiuso
            if (*fd == *fdMax) aggiornaMax(clients, fdMax); //se il fd era il massimo devo risettarlo
            close(*fd); //chiudo il socket   
            pthread_mutex_unlock(mtx);
            free(fd);
            break; 
            }    
    }

    //(*conn)++; //quando il thread termina abbiamo completato un servizio
    return NULL; 
}

int main (int argc, char** argv){

   //printf ("n\tavg\tdev\tfile\n-----------------------------------------------------------\n");
   // fflush(stdout);

    int server = socket (AF_INET, SOCK_STREAM,0); //restituisce un fd
    if (server < 0){
        printf ("Errore creazione del socket"); 
        exit (EXIT_FAILURE); 
    }
    
    //devo settare i socket e fare le connessioni
    struct sockaddr_in serverAddr;
    serverAddr.sin_family=AF_INET; 
    serverAddr.sin_port=htons(PORT);
    serverAddr.sin_addr.s_addr=inet_addr("172.27.68.197");

    int numThread = atoi(argv[2]);
    int r1,r2;
    SYSCALL_EXIT(bind,r1,bind (server,(struct sockaddr*)&serverAddr, sizeof(serverAddr)), " sulla bind\n"); 
    SYSCALL_EXIT(listen, r2,listen (server, numThread + 1), " sulla listen\n"); //mi metto in ascolto del numero massimo di file che possono esserci dentro la dir corrente

    fd_set readFDs, clFDs; //fdset usata dal thread server principale

    fd_set* closed = (fd_set*) malloc (sizeof(fd_set));
    FD_ZERO(closed);
    
    fd_set* allFDs = (fd_set*) malloc (sizeof(fd_set)); 
    FD_ZERO(allFDs); 
    FD_SET(server,allFDs); 
    int fdMax = server;

    pthread_mutex_t* m = (pthread_mutex_t*) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init(m, NULL);   
    Queue_t* q = initQueue(); 
     
    arg_t* threadArgs = (arg_t*) malloc (sizeof (arg_t)); 
    threadArgs->q=q;
    threadArgs->mutex=m;
    threadArgs->fdMax=&fdMax;
    threadArgs->clients=allFDs;
    threadArgs->closed=closed;
    threadArgs->connections = (int*) malloc (sizeof (int));
    *(threadArgs->connections) = 0; 
    
    //creiamo un pool di thread
    pthread_t tid[numThread];
    for (int i = 0; i < numThread; i++){
        int err;      
        SYSCALL_EXIT(pthread_create,err,pthread_create(&tid[i], NULL, worker, threadArgs), " sulla creazione del %d thread server\n", i);
       // pthread_detach(tid[i]); //so che il server deve rimanere in attesa, così forzo il fatto che non possa fare la join sui thread
    }
     
    int clientConnessi[numThread]; //array che uso per tenere traccia dei fd aperti (clients)
    //inizializzazione
    for (int i = 0; i < numThread; i++){
        clientConnessi[i] = 0; 
    }
    int k = 0;

    //questo è il thread che rappresenta il server: rimane in attesa fino a quando l'operazione di scrittura dell'output non è completata
    while (1){
    pthread_mutex_lock (m); 
    readFDs = *(threadArgs->clients); //copia del set
    clFDs = *(threadArgs->closed);
    int currMax = *threadArgs->fdMax;  
    pthread_mutex_unlock(m);

    for (int i = 0; i < currMax+1; i++){
        if (FD_ISSET(i, &clFDs)){
            pthread_mutex_lock(m);
            FD_CLR(i, &readFDs);
            pthread_mutex_unlock(m); 
        }
    }

    //for (int i = 0; i < currMax+1; i++){
    //    if (FD_ISSET(i, &readFDs)) printf ("%d ",i); 
    //}
    //printf ("\n");
    //printf ("%d\n", *threadArgs->connections);
    //printf ("%ld \n", length(threadArgs->q));

    if (*(threadArgs->connections) == numThread) break; 

    int r; 
    SYSCALL_EXIT(select, r, select(currMax+1,&readFDs,NULL,NULL,NULL), "Facendo la Select\n"); //aspetta che un qualche fd in readFDs sia pronto per la lettura

    for (int i = 0; i < currMax + 1;i++){
        if (FD_ISSET(i, &readFDs)){
          
            if (i == server){ //server pronto
                int clientFD = accept(server, NULL, NULL); //ritorna il fd usato per la connessione con il client
                
                pthread_mutex_lock(m); 
                FD_SET( clientFD,threadArgs->clients); //metto il client in allFDs
                if(clientFD > fdMax) *(threadArgs->fdMax) = clientFD; //aggiorno fdMax in questo caso
                pthread_mutex_unlock(m);  
            }else {
                //read : bisogna tenere traccia dei client che sono già in fase di connessione con i thread
                int flag = 0; 
                for (int j = 0; j < numThread;j++){
                    if (clientConnessi[j] == i) flag = 1;
                }

                if (flag) continue; //passo al prossimo poichè è già dentro la coda
 
                clientConnessi[k] = i; //lo metto dentro l'array
                k++; 
                int* client = (int*) malloc(sizeof (int)); 
                *client = i; 
                push(threadArgs->q,client);
                (*threadArgs->connections)++;
                }
            }
        } 
    }

    //attendo che i thread terminino visto che non accetto più altre connessioni 
    for (int i = 0; i < numThread; i++){
        pthread_join(tid[i], NULL);
    }

    int cl; 
    SYSCALL_EXIT(close, cl, close(server), " sulla chiusura del server");

    free(closed);
    free(allFDs);
    deleteArgs(threadArgs);
    return 0;
}