#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "babble_publication_set.h"
#include "babble_server.h"

int readerWaiting_pub = 0;
int readerCount_pub = 0;
int writerWaiting_pub = 0;
int writer_pub = 0;
pthread_mutex_t mutex_pub = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_read_pub = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_write_pub = PTHREAD_COND_INITIALIZER;

void startWrite_pub(void){
    pthread_mutex_lock(&mutex_pub);
    writerWaiting_pub = writerWaiting_pub + 1;
    while((readerCount_pub > 0) || (writer_pub == 1)){
        pthread_cond_wait(&cond_write_pub, &mutex_pub);
    }
    writerWaiting_pub = writerWaiting_pub - 1;
    writer_pub = 1;
    pthread_mutex_unlock(&mutex_pub);
}

void endWrite_pub(void){
    pthread_mutex_lock(&mutex_pub);
    writer_pub = 0;
    if(writerWaiting_pub > 0){
        pthread_cond_signal(&cond_write_pub);
    }else if(readerWaiting_pub > 0){
        pthread_cond_broadcast(&cond_read_pub);
    }
    pthread_mutex_unlock(&mutex_pub);
}

void startRead_pub(void){
    pthread_mutex_lock(&mutex_pub);
    readerWaiting_pub = readerWaiting_pub + 1;
    while((writerWaiting_pub > 0) || (writer_pub == 1)){
        pthread_cond_wait(&cond_read_pub, &mutex_pub);
    }
    readerWaiting_pub = readerWaiting_pub - 1;
    readerCount_pub = readerCount_pub + 1;
    pthread_mutex_unlock(&mutex_pub);
}

void endRead_pub(void){
    pthread_mutex_lock(&mutex_pub);
    readerCount_pub = readerCount_pub - 1;
    if((readerCount_pub == 0) && (writerWaiting_pub > 0)){
        pthread_cond_signal(&cond_write_pub);
    }
    pthread_mutex_unlock(&mutex_pub);
}

publication_set_t* publication_set_create(void)
{
    publication_set_t* new_set= malloc(sizeof(publication_set_t));
    new_set->first = NULL;
    new_set->last = NULL;

    return new_set;
}


publication_t* publication_set_insert(publication_set_t *set, char* msg)
{
    startWrite_pub();

    struct timespec tt;

    publication_t *pub= malloc(sizeof(publication_t));

    strncpy(pub->msg, msg, BABBLE_SIZE);
    pub->next=NULL;

    clock_gettime(CLOCK_REALTIME, &tt);

    pub->date= tt.tv_sec - server_start;
    pub->ndate = (uint64_t)1000000000 * tt.tv_sec + tt.tv_nsec;

    /* inserting the new publication in list */
    if(set->first == NULL){
        set->first = pub;
        set->last = pub;
    }
    else{
        set->last->next = pub;
        set->last = pub;
    }

    endWrite_pub();

    return pub;
}

publication_t* publication_set_getnext(publication_set_t *set, publication_t* last_pub, uint64_t min_date)
{

	startRead_pub();

    if(last_pub != NULL){
        if(last_pub->next == NULL || last_pub->next->ndate >= min_date){
            endRead_pub();
            return last_pub->next;
        }
    }

    publication_t *item = set->first;

    while(item != NULL && item->ndate < min_date){
        item = item -> next;
    }

	endRead_pub();

    return item;
}
