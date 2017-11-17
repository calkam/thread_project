#include "babble_communication.h"
#include "babble_types.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

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

/* writing data of file descriptor */
static int write_data(int fd, unsigned long size, void* buf)
{
    //startWrite();

    unsigned long total_sent=0;

    do {
        total_sent += write(fd, ((char*) buf+total_sent), size - total_sent);
    } while(total_sent < size && errno != EINTR );


    if(total_sent < size){
        fprintf(stderr,"sent only %lu/%lu bytes\n", total_sent, size);
    }

    //endWrite();

    return (total_sent == size) ? total_sent : -1;
}

/* reading data on file descriptor */
static int read_data(int fd, unsigned long size, void* buf)
{
    //startRead();

    unsigned long total_recv=0;

    do {
        total_recv += read(fd, ((char*) buf+total_recv), size - total_recv);
    } while(total_recv < size && errno == EINTR);

    if(total_recv < size){
        fprintf(stderr,"received only %lu/%lu bytes\n", total_recv, size);
    }

    //endRead();

    return (total_recv == size) ? total_recv : -1;
}

int network_send(int fd, unsigned long size, void* buf)
{
    startWrite();

    if(write_data(fd, sizeof(unsigned long), &size) != sizeof(unsigned long)){
        perror("writing on socket");
        return -1;
    }


    if(write_data(fd, size, buf) != size){
        perror("writing on socket");
        return -1;
    }

    endWrite();

    return size;
}


int network_recv(int fd, void **buf)
{
    unsigned long payload_size = 0;
    int r=0;

    if((r=read_data(fd, sizeof(unsigned long), &payload_size)) != sizeof(unsigned long)){
        fprintf(stderr,"error recv: expected %lu received %d\n", sizeof(unsigned long), r);
        return -1;
    }

    char* recv_buf = (char*) malloc(payload_size);

    if((r=read_data(fd, payload_size, recv_buf)) != payload_size){
        fprintf(stderr,"error recv: expected %lu received %d\n", payload_size, r);
        return -1;
    }

    *buf = (void*)recv_buf;

    return payload_size;
}
