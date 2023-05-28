#include "unboundedqueue.h"
#include "util.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
#define N 1024
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
        float el = (media - a[i])*(media - a[i]); 
        dev += el; 
    }
    return sqrt(dev/length); 
}

char* formatta (char* s){
    char* delimitatori = " \t\n";
    char* temp = strdup(s);
    char* tok;
    char* res = (char*) malloc ((strlen(s)+1)*sizeof(char)); 
    memset(res, 0, strlen(s)); 

    if((tok = strtok(temp,delimitatori)) != NULL){
        strcat(res,tok); 
        while ((tok = strtok(NULL, delimitatori)) != NULL){
        strcat(res,tok);
        }
    }

    if(strlen(res) == 0){
        free(res);
        return NULL;
    }  
    return res; 
}

char* elaboraDati(char* pathFile) {
    FILE* f;
    if ((f = fopen(pathFile, "r")) == NULL) {
        perror("open file error");
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LENGTH_PATH];
    float* arr = (float*) calloc(MAX_LENGTH_PATH,sizeof(float));
    int count = 0;
    //char* r = (char*) malloc (MAX_LENGTH_PATH * sizeof (char)); 
    while (fgets(buffer, MAX_LENGTH_PATH, f) != NULL) {  
       char* r = formatta(buffer); 
        
        if (r != NULL) {
            count++;
            float* new_arr = (float*)realloc(arr, count * sizeof(float));
            if (new_arr == NULL) {
                perror("errore nella realloc");
                free(arr); // Dealloco la memoria precedentemente allocata per arr
                exit(EXIT_FAILURE);
            }
            arr = new_arr;
            float number = strtof(r,NULL);
            *(arr + count - 1) = number;  
        }

       free(r); // Dealloco la memoria allocata per r
      // r = (char*) malloc ((MAX_LENGTH_PATH) * sizeof (char)); //devo allocare tanti blocchi per evitare di prendere in considerazione memoria invalida e creare numeri scorretti
    }
    //free(r); 

    float m = media(arr, count);
    float dev = deviazione(arr, m, count);
    char* output = (char*) calloc (MAX_FILENAME_LENGTH,sizeof(char)); 
    sprintf(output,"%d   %.2f   %.2f   %s \n", count, m, dev, pathFile);

    free(arr);
    fclose(f);
    return output;
}


void pathVisit(char* dir, Queue_t* q){
    DIR* d; 
    if ((d = opendir(dir)) == NULL){
        perror ("opendir"); 
        exit (EXIT_FAILURE); 
    }//siamo sicuri che d è una directory

    struct dirent* dr;
    char *newPath = (char*) malloc (MAX_LENGTH_PATH * sizeof(char));

    while ((errno=0,dr = readdir(d)) != NULL){
        struct stat info;
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
                        pathVisit(newPath, q);
            }     
        }

        if (S_ISREG(info.st_mode)){
            if (strstr(newPath, ".dat") != NULL){
                push(q,strdup(newPath));
            } 
        }   
    }

    free(newPath); 
    closedir(d);   
    return; 
}

//client
void* thread_worker(void* arg){
    /**thread che rappresenta un client: richiede connessione al server tramite i dati della bind, invia i messaggi da far stampare al server e poi
    * e poi chiude la connessione chiudendo il socket fd
    */
    int client = socket (AF_INET,SOCK_STREAM,0);
    if (client < 0){
        printf ("Errore creazione del socket"); 
        exit (EXIT_FAILURE); 
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family=AF_INET; 
    serverAddr.sin_port=htons(PORT);
    serverAddr.sin_addr.s_addr=inet_addr("172.27.68.197");
    int conn = 0; 

    while ((conn = connect(client,(struct sockaddr*)&serverAddr, sizeof(serverAddr))) == -1 && errno == ENOENT){sleep(1);};
    if (conn == -1){
        close(client);
        perror ("errore di connessione"); 
        exit(EXIT_FAILURE); 
    }

    arg_t* a = (arg_t*) arg;
    Queue_t* q = a->q;
    char* file = (char*) calloc (MAX_LENGTH_PATH,sizeof(char));

    while (1){
        pthread_mutex_lock(a->mtx);
        strcpy(file,(char*) pop(q));  

        //printf ("%s\n", file);
        if (strcmp (file, "fine") == 0){ 
            pthread_mutex_unlock(a->mtx); 
            break;
        }   

        char* output = elaboraDati(file);
        write (client, output, (strlen(output)+1)*sizeof(char)); //messaggio al collector 
        read (client, output, (strlen(output)+1)*sizeof(char)); //leggo il messaggio dal server
        pthread_mutex_unlock(a->mtx); 
        free(output);
    }
    free(file);
    
     
    int cl; 
    SYSCALL_EXIT(close, cl, close(client), " sulla chiusura del client");
    return NULL;  
}

int main (int argc, char** argv){

    if (argc != 3){
        fprintf (stdout, "Numeri di parametri non corretto\n");
        exit(EXIT_FAILURE); 
    }
    
   //pid_t pid; 
   // SYSCALL_EXIT (fork, pid, fork(), "sulla fork"); 
   // if (pid == 0){
   //     execv("./c.out", argv); 
   //     perror ("cannot exec"); 
   //     exit(EXIT_FAILURE); 
   // }

    int workers = atoi(argv[2]); 
    pthread_t tid[workers]; 
    Queue_t* q; 
    pthread_mutex_t* mtx = (pthread_mutex_t*)malloc (sizeof(pthread_mutex_t)); 
    pthread_mutex_init(mtx, NULL); 
    q = initQueue();
 
    char* dir = argv[1];    
    pathVisit(dir, q); //inserimento del nome dei file dentro la coda
    for (int i = 0; i < workers; i++){
        push(q, "fine");
    }

    
    arg_t arg = {q,mtx};

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
    free(mtx);
    return 0; 
}