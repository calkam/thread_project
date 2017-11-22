#include <stdio.h>
#include <strings.h>
#include <pthread.h>
#include <semaphore.h>

#include "babble_registration.h"

client_bundle_t *registration_table[MAX_CLIENT];
int nb_registered_clients;

int readerWaiting = 0;
int readerCount = 0;
int writerWaiting = 0;
int writer = 0;
pthread_mutex_t mutex_reg = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_read = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_write = PTHREAD_COND_INITIALIZER;

void startWrite(void){
    pthread_mutex_lock(&mutex_reg);
    writerWaiting = writerWaiting + 1;
    while((readerCount > 0) || (writer == 1)){
        pthread_cond_wait(&cond_write, &mutex_reg);
    }
    writerWaiting = writerWaiting - 1;
    writer = 1;
    pthread_mutex_unlock(&mutex_reg);
}

void endWrite(void){
    pthread_mutex_lock(&mutex_reg);
    writer = 0;
    if(writerWaiting > 0){
        pthread_cond_signal(&cond_write);
    }else if(readerWaiting > 0){
        pthread_cond_broadcast(&cond_read);
    }
    pthread_mutex_unlock(&mutex_reg);
}

void startRead(void){
    pthread_mutex_lock(&mutex_reg);
    readerWaiting = readerWaiting + 1;
    while((writerWaiting > 0) || (writer == 1)){
        pthread_cond_wait(&cond_read, &mutex_reg);
    }
    readerWaiting = readerWaiting - 1;
    readerCount = readerCount + 1;
    pthread_mutex_unlock(&mutex_reg);
}

void endRead(void){
    pthread_mutex_lock(&mutex_reg);
    readerCount = readerCount - 1;
    if((readerCount == 0) && (writerWaiting > 0)){
        pthread_cond_signal(&cond_write);
    }
    pthread_mutex_unlock(&mutex_reg);
}

void registration_init(void)
{
    nb_registered_clients=0;

    bzero(registration_table, MAX_CLIENT * sizeof(client_bundle_t*));
}

client_bundle_t* registration_lookup(unsigned long key)
{
    int i=0;

    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            return registration_table[i];
        }
    }

    return NULL;
}

int registration_insert(client_bundle_t* cl)
{
    startWrite();

    if(nb_registered_clients == MAX_CLIENT){
        //WARNING : ENDWRITE doesn't call
        return -1;
    }

    /* lookup to find if key already exists*/
    client_bundle_t* lp= registration_lookup(cl->key);
    if(lp != NULL){
        fprintf(stderr, "Error -- id % ld already in use\n", cl->key);
        return -1;
    }

    /* insert cl */
    registration_table[nb_registered_clients]=cl;
    nb_registered_clients++;

    endWrite();

    return 0;
}


client_bundle_t* registration_remove(unsigned long key)
{
    startRead();

    int i=0;

    for(i=0; i<nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            break;
        }
    }

    if(i == nb_registered_clients){
        fprintf(stderr, "Error -- no client found\n");
        return NULL;
    }


    client_bundle_t* cl= registration_table[i];

    nb_registered_clients--;
    registration_table[i] = registration_table[nb_registered_clients];

    endRead();

    return cl;
}
