#include <stdio.h>
#include <strings.h>
#include <pthread.h>
#include <semaphore.h>

#include "babble_registration.h"

client_bundle_t *registration_table[MAX_CLIENT];
int nb_registered_clients;

sem_t S1;
sem_t S2;
sem_t S3;

void registration_init(void)
{
    nb_registered_clients=0;

    sem_init(&S1, 0, 1);
    sem_init(&S2, 0, MAX_CLIENT);
    sem_init(&S3, 0, 0);

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
    sem_wait(&S2);
    sem_wait(&S1);

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

    sem_post(&S3);
    sem_post(&S1);

    return 0;
}


client_bundle_t* registration_remove(unsigned long key)
{
    sem_wait(&S3);
    sem_wait(&S1);

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

    sem_post(&S2);
    sem_post(&S1);

    return cl;
}
