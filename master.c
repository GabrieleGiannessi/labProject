#include "unboundedqueue.h"
#include "util.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
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
/**
 * 
 * Il processo master, prima di creare i thread worker, attraverso una BFS inserisce dentro una struttura dati sincronizzata i dati (nome dei file)
 * I thread worker si occuperanno di accedere alla struttura in modo sincronizzato (usiamo i metodi implementati nella libreria unboundedqueue.h, che risultano essere thread safe)
 * e recuperano i dati, che manderanno al collector attraverso l'uso dei socket.
 * 
 * N.B -> i dati vengono mandati al collector solo quando sono stati finiti di elaborare
 * Devo creare una struttura da condividere tra thread (dentro il produttore)
 * 
 * 
 * Worker -> client
 * 
*/

typedef struct{
    Queue_t* q; 
    pthread_mutex_t* mtx;
    int sfd; //socket file descriptor del client
}arg_t; 


float media (float* a,int length){
    float sum = 0; 
    for (int i = 0; i < length; i++){
        sum += *(a+i); 
    }  

    return sum/length;     
}

float deviazione(float* a, float media, int length){
    float dev = 0; 
    for (int i = 0; i < length; i++){
        dev += (float)pow((media - a[i]),2); 
    }
    return (float)sqrt(dev/length); 
}

float elaboraFloat(char f[]){ //elabora i numeri dentro al file e restituisce i numeri per calcolare  
      float n; 
      sscanf (f,"%f",&n); 
      return n; 
}

char* formatta (char* s){
    char* delimitatori = " \t\n";
    char* temp = strdup(s);
    char* tok;
    char* res = (char*) malloc (MAX_LENGTH_PATH*sizeof(char));  

    if((tok = strtok(temp,delimitatori)) != NULL){
        strcat(res,tok); 
        while ((tok = strtok(NULL, delimitatori)) != NULL){
        strcat(res,tok);
        }
    }
    if(strlen(res) == 0) return NULL; 
    return res; 
}

char* elaboraDati (char* pathFile){
    FILE* f; 
    if ((f = fopen (pathFile, "r")) == NULL){  
        perror ("open file error"); 
        exit(EXIT_FAILURE);
    }

    char buffer [MAX_LENGTH_PATH]; 
    float* arr = (float*) malloc(sizeof(float));; 
    int i = 0; 
    int count = 0;

        while (fgets (buffer, MAX_LENGTH_PATH, f) != NULL){
            char* r = formatta (buffer); 
            if (r != NULL){
            count++;
            arr = (float*) realloc (arr, count*sizeof(float)); 
            float number = elaboraFloat(r); 
            //array per contenere i numeri elaborati     
            *(arr + i) = number; 
            i++;
            } 
        }
        float m = media(arr, count); 
        float dev = deviazione (arr, m, count);
        char* line = (char*) malloc (MAX_LENGTH_PATH* sizeof (char)); 
        sprintf (line,"%d   %.2f   %.2f   %s \n",count,m,dev,pathFile);
        return line;  
}


void pathVisit(char* dir, Queue_t* q){
    DIR* d; 
    if ((d = opendir(dir)) == NULL){
        perror ("opendir"); 
        exit (EXIT_FAILURE); 
    }//siamo sicuri che d è una directory

    struct dirent* dr; 
    while ((errno=0,dr = readdir(d)) != NULL){
        struct stat info;  
        char *newPath = (char*) malloc (MAX_LENGTH_PATH * sizeof (char)); 
        int l1 = strlen (dir); 
        int l2 = strlen (dr->d_name); 
        if (l1 + l2 + 1 > MAX_LENGTH_PATH) {
            fprintf (stdout, "Path troppo lungo\n"); 
            exit(EXIT_FAILURE); 
        } 

        strcpy (newPath, dir); 
        strcat (newPath, "/"); 
        strcat (newPath, dr->d_name);

 

        if (stat (newPath, &info) == -1){
            perror ("Errore nella stat"); 
            exit(EXIT_FAILURE);
        } 

        if (S_ISDIR (info.st_mode)){
            int l = strlen (newPath); 
            if (!(l > 0 && newPath[l-1] == '.')){
                        pathVisit (newPath, q);
            }     
        }

        if (S_ISREG(info.st_mode)){
            if (strstr(newPath, ".dat") != NULL){
                push (q,newPath);
            } 
        }
    }

    closedir(d);  
    free(d); 
    return; 
}

//client
void* thread_worker(void* arg){
    /**thread che rappresenta un client: richiede connessione al server tramite i dati della bind, invia i messaggi da far stampare al server e poi
    * e poi chiude la connessione chiudendo il socket 
    */
   
    arg_t* a = (arg_t*) arg;
    Queue_t* q = a->q;
    int sfd = (int) a->sfd; 
    while (1){
        pthread_mutex_lock(a->mtx);
        char* file = (char*) pop (q); 

        if (strcmp (file, "fine") == 0){ 
            pthread_mutex_unlock(a->mtx); 
            break;
        }   

        char* line = elaboraDati(file);
        free (file); 
        
        write (sfd, line, (strlen(line)+1)*sizeof(char)); //messaggio al collector 
        read (sfd, line, (strlen(line)+1)*sizeof(char)); //leggo il messaggio dal server
        pthread_mutex_unlock(a->mtx); 
        free(line); 
    }

    return NULL;  
}

int main (int argc, char** argv){

    if (argc != 3){
        fprintf (stdout, "Numeri di parametri non corretto\n");
        exit(EXIT_FAILURE); 
    }
    
   pid_t pid; 
    SYSCALL_EXIT (fork, pid, fork(), "sulla fork"); 
    if (pid == 0){
        execv("c.out", argv); 
        perror ("cannot exec"); 
        exit(EXIT_FAILURE); 
    }

    int workers = atoi(argv[2]); 
    pthread_t tid[workers]; 
    Queue_t* q; 
    pthread_mutex_t* mtx = (pthread_mutex_t*)malloc (sizeof(pthread_mutex_t)); 
    pthread_mutex_init (mtx, NULL); 
    q = initQueue();
 
    char* dir = argv[1];    
    pathVisit(dir, q); //inserimento del nome dei file dentro la coda
    for (int i = 0; i < workers; i++){
        push(q, "fine");
    }
    
    int server = socket (AF_INET,SOCK_STREAM,0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family=AF_INET; 
    serverAddr.sin_port=htons(1111);
    serverAddr.sin_addr.s_addr=inet_addr("172.27.68.197");
    int conn = 0; 

    while ((conn = connect(server,(struct sockaddr*)&serverAddr, sizeof(serverAddr))) == -1 && errno == ENOENT){sleep(1);};
    if (conn == -1){
        printf ("errore di connessione\n"); 
        exit(EXIT_FAILURE); 
    }

    arg_t arg = {q,mtx,server};

    //workers
    for (int i = 0; i < workers; i++){
        int err; 
        if ((err = pthread_create(&tid[i],NULL,thread_worker,&arg)) != 0){
            perror ("pthread create"); 
            exit (EXIT_FAILURE); 
        }
    }

    for (int i = 0; i < workers;i++){
        pthread_join(tid[i],NULL); 
    }

    deleteQueue(q);
    pthread_mutex_destroy(mtx); 
    close (server); 
    return 0; 
}
