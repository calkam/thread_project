#ifndef __BABBLE_SERVER_H__
#define __BABBLE_SERVER_H__

#include <stdio.h>

#include "babble_types.h"

#define BUFFER_SIZE 40
#define NB_THREAD_MAX 40

/* server starting date */
extern time_t server_start;

/* Init functions*/
void server_data_init(void);
int server_connection_init(int port);
int server_connection_accept(int sock);

/* new object */
command_t* new_command(unsigned long key);

void push_buffer(command_t cmd);
command_t pop_buffer(void);

void* thread_communication(void* arg);
void* thread_executor(void* arg);

/* Operations */
int run_login_command(command_t *cmd);
int run_publish_command(command_t *cmd);
int run_follow_command(command_t *cmd);
int run_timeline_command(command_t *cmd);
int run_fcount_command(command_t *cmd);
int run_rdv_command(command_t *cmd);

int unregisted_client(command_t *cmd);

/* Display functions */
void display_command(command_t *cmd, FILE* stream);

/* Error management */
int notify_parse_error(command_t *cmd, char *input);

/* High level comm function */
int write_to_client(unsigned long key, int size, void* buf);

#endif
