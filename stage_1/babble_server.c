#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#include "babble_server.h"
#include "babble_types.h"
#include "babble_utils.h"
#include "babble_communication.h"

pthread_mutex_t mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  non_full  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  non_empty = PTHREAD_COND_INITIALIZER;

command_t* buffer[BUFFER_SIZE];

int nb_element = 0;
int in=0;
int out=0;

pthread_t tids[MAX_CLIENT];
int nb_thread=0;

static void display_help(char *exec)
{
    printf("Usage: %s -p port_number\n", exec);
}

static int parse_command(char* str, command_t *cmd)
{
    /* start by cleaning the input */
    str_clean(str);

    /* get command id */
    cmd->cid=str_to_command(str, &cmd->answer_expected);

    /* initialize other fields */
    cmd->answer.size=-1;
    cmd->answer.aset=NULL;

    switch(cmd->cid){
    case LOGIN:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            fprintf(stderr,"Error -- invalid LOGIN -> %s\n", str);
            return -1;
        }
        break;
    case PUBLISH:
        if(str_to_payload(str, cmd->msg, BABBLE_SIZE)){
            fprintf(stderr,"Warning -- invalid PUBLISH -> %s\n", str);
            return -1;
        }
        break;
    case FOLLOW:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            fprintf(stderr,"Warning -- invalid FOLLOW -> %s\n", str);
            return -1;
        }
        break;
    case TIMELINE:
        cmd->msg[0]='\0';
        break;
    case FOLLOW_COUNT:
        cmd->msg[0]='\0';
        break;
    case RDV:
        cmd->msg[0]='\0';
        break;
    default:
        fprintf(stderr,"Error -- invalid client command -> %s\n", str);
        return -1;
    }

    return 0;
}

static int process_command(command_t *cmd)
{
    int res=0;

    switch(cmd->cid){
    case LOGIN:
        res = run_login_command(cmd);
        break;
    case PUBLISH:
        res = run_publish_command(cmd);
        break;
    case FOLLOW:
        res = run_follow_command(cmd);
        break;
    case TIMELINE:
        res = run_timeline_command(cmd);
        break;
    case FOLLOW_COUNT:
        res = run_fcount_command(cmd);
        break;
    case RDV:
        res = run_rdv_command(cmd);
        break;
    default:
        fprintf(stderr,"Error -- Unknown command id\n");
        return -1;
    }

    if(res){
        fprintf(stderr,"Error -- Failed to run command ");
        display_command(cmd, stderr);
    }

    return res;
}

/* sends an answer for the command to the client if needed */
/* answer to a command is stored in cmd->answer after the command has
 * been processed. They are different cases
 + The client does not expect any answer (then nothing is sent)
 + The client expect an answer -- 2 cases
  -- The answer is a single msg
  -- The answer is potentially composed of multiple msgs (case of a timeline)
*/
static int answer_command(command_t *cmd)
{
    /* case of no answer requested by the client */
    if(!cmd->answer_expected){
        if(cmd->answer.aset != NULL){
            free(cmd->answer.aset);
        }
        return 0;
    }

    /* no msg to be sent */
    if(cmd->answer.size == -2){
        return 0;
    }

    /* a single msg to be sent */
    if(cmd->answer.size == -1){
        /* strlen()+1 because we want to send '\0' in the message */
        if(write_to_client(cmd->key, strlen(cmd->answer.aset->msg)+1, cmd->answer.aset->msg)){
            fprintf(stderr,"Error -- could not send ack for %d\n", cmd->cid);
            free(cmd->answer.aset);
            return -1;
        }
        free(cmd->answer.aset);
        return 0;
    }

    /* a set of msgs to be sent */
    /* number of msgs sent first */
    if(write_to_client(cmd->key, sizeof(int), &cmd->answer.size)){
        fprintf(stderr,"Error -- send set size: %d\n", cmd->cid);
        return -1;
    }

    answer_t *item = cmd->answer.aset, *prev;
    int count=0;

    /* send only the last BABBLE_TIMELINE_MAX */
    int to_skip= (cmd->answer.size > BABBLE_TIMELINE_MAX)? cmd->answer.size - BABBLE_TIMELINE_MAX : 0;

    for(count=0; count < to_skip; count++){
        prev=item;
        item = item->next;
        free(prev);
    }

    while(item != NULL ){
        if(write_to_client(cmd->key, strlen(item->msg)+1, item->msg)){
            fprintf(stderr,"Error -- could not send set: %d\n", cmd->cid);
            return -1;
        }
        prev=item;
        item = item->next;
        free(prev);
        count++;
    }

    assert(count == cmd->answer.size);
    return 0;
}

//------------------------- THREAD --------------------------------------------

void push_buffer(command_t *cmd){
    //TODO give priority to the first messages

    command_t *cmd_clone = clone_command(cmd);

    pthread_mutex_lock(&mutex);

    while(nb_element == BUFFER_SIZE){
        pthread_cond_wait(&non_full, &mutex);
    }

    buffer[in] = cmd_clone;
    in = (in + 1) % BUFFER_SIZE;
    nb_element++;

    pthread_cond_broadcast(&non_empty);

    pthread_mutex_unlock(&mutex);

    return;
}

command_t* pop_buffer(void){

    command_t *cmd;

    pthread_mutex_lock(&mutex);

    while(nb_element == 0){
        pthread_cond_wait(&non_empty, &mutex);
    }

    cmd = buffer[out];
    out = (out + 1) % BUFFER_SIZE;
    nb_element--;

    pthread_cond_broadcast(&non_full);

    pthread_mutex_unlock(&mutex);

    return cmd;
}

void* thread_communication(void* arg){

    int newsockfd = *((int*) arg);

    command_t *cmd;
    unsigned long client_key=0;
    char client_name[BABBLE_ID_SIZE+1];

    int recv_size=0;
    char* recv_buff=NULL;

    bzero(client_name, BABBLE_ID_SIZE+1);
    if((recv_size = network_recv(newsockfd, (void**)&recv_buff)) < 0){
        fprintf(stderr, "Error -- recv from client\n");
        close(newsockfd);
        return NULL;
    }

    cmd = new_command(0);

    if(parse_command(recv_buff, cmd) == -1 || cmd->cid != LOGIN){
        fprintf(stderr, "Error -- in LOGIN message\n");
        close(newsockfd);
        free(cmd);
        return NULL;
    }

    /* before processing the command, we should register the
     * socket associated with the new client; this is to be done only
     * for the LOGIN command */
    cmd->sock = newsockfd;

    if(process_command(cmd) == -1){
        fprintf(stderr, "Error -- in LOGIN\n");
        close(newsockfd);
        free(cmd);
        return NULL;
    }

    /* notify client of registration */
    if(answer_command(cmd) == -1){
        fprintf(stderr, "Error -- in LOGIN ack\n");
        close(newsockfd);
        free(cmd);
        return NULL;
    }

    /* let's store the key locally */
    client_key = cmd->key;

    strncpy(client_name, cmd->msg, BABBLE_ID_SIZE);
    free(recv_buff);
    free(cmd);

    /* looping on client commands */
    while((recv_size=network_recv(newsockfd, (void**) &recv_buff)) > 0){

        cmd = new_command(client_key);
        if(parse_command(recv_buff, cmd) == -1){
            fprintf(stderr, "Warning: unable to parse message from client %s\n", client_name);
            notify_parse_error(cmd, recv_buff);
        }else{
            push_buffer(cmd);
        }
        free(recv_buff);
        free(cmd);
    }

    if(client_name[0] != 0){
        cmd = new_command(client_key);
        cmd->cid= UNREGISTER;

        if(unregisted_client(cmd)){
            fprintf(stderr,"Warning -- failed to unregister client %s\n",client_name);
        }
        free(cmd);
    }

    return NULL;
}

void* thread_executor(void* arg){

    command_t *cmd;

    while(1){
        cmd = pop_buffer();
        if(process_command(cmd) == -1){
            //TODO : client_key
            //fprintf(stderr, "Warning: unable to process command from client %lu\n", client_key);
        }
        if(answer_command(cmd) == -1){
            //fprintf(stderr, "Warning: unable to answer command from client %lu\n", client_key);
        }
    }

    return NULL;
}

//------------------------- JAMY --------------------------------------------

int main(int argc, char *argv[])
{
    int sockfd, newsockfd;
    int portno=BABBLE_PORT;

    int opt;
    int nb_args=1;

    while ((opt = getopt (argc, argv, "+p:")) != -1){
        switch (opt){
        case 'p':
            portno = atoi(optarg);
            nb_args+=2;
            break;
        case 'h':
        case '?':
        default:
            display_help(argv[0]);
            return -1;
        }
    }

    if(nb_args != argc){
        display_help(argv[0]);
        return -1;
    }

    server_data_init();

    if((sockfd = server_connection_init(portno)) == -1){
        return -1;
    }

    printf("Babble server bound to port %d\n", portno);

    /* main server loop */
    while(1){

        if((newsockfd = server_connection_accept(sockfd))==-1){
            return -1;
        }

        if(pthread_create(&tids[nb_thread], NULL, thread_communication, &newsockfd) != 0){
            fprintf(stderr,"Failed to create thread number %d\n", nb_thread);
        }else{
            nb_thread++;
        }

        if(pthread_create(&tids[nb_thread], NULL, thread_executor, NULL) != 0){
            fprintf(stderr,"Failed to create thread number %d\n", nb_thread);
        }else{
            nb_thread++;
        }

    }
    close(sockfd);
    return 0;
}
