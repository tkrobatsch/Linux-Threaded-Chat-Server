/*
labA
Tom Krobatsch
April 21, 2019

jtalk
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "fields.h"
#include "jval.h"
#include "dllist.h"
#include "jrb.h"
#include "sockettome.h"

JRB rooms;

typedef struct {
	char* name;
	Dllist clients;
	Dllist msgs;
	pthread_mutex_t* lock;
	pthread_cond_t* wait;
}Room;

typedef struct {
	char* name;
	FILE* in;
	FILE* out;
}Client;

void* process_rooms(void* node) {
	//varibles
	Room* passed_node;
	Client* temp_client;
	int fputs_test, fflush_test;
	Dllist msg_list;
	Dllist client_list;
	Dllist delete_client;
	char* print;

	//init varibles
	passed_node = (Room*)node;

	//lock and loop
	pthread_mutex_lock(passed_node->lock);
	while (1) {
		pthread_cond_wait(passed_node->wait, passed_node->lock);
		//while there are messages
		while (!dll_empty(passed_node->msgs)) {
			msg_list = dll_first(passed_node->msgs);
			//catch for no clients
			if (!dll_empty(passed_node->clients)) {
				//print message and error catch
				dll_traverse(client_list, passed_node->clients) {
					delete_client = dll_first(passed_node->clients);
					temp_client = (Client*)client_list->val.v;
					printf("%s", msg_list->val.s);
					fputs_test = fputs(msg_list->val.s, temp_client->out);
					fflush_test = fflush(temp_client->out);
					if ((fputs_test == EOF) || (fflush_test == EOF)) {
						printf("should be deleting client\n");
						dll_delete_node(delete_client);
						fclose(temp_client->out);
					}
				}
			}
			//delete message now that its been printed
			free(msg_list->val.s);
			dll_delete_node(msg_list);
		}
	}
}

void* process_clients(void* node) {
	//varibles
	JRB temp_jrb;
	Room* temp_room;
	Client* temp_client, * print_client;
	Dllist client_list, delete_client;
	char client_name[256];
	char room_name[256];
	char input[1000];
	char* message;
	int test;
	int* file_pointer;
	file_pointer = (int*)node;

	//set up client
	temp_client = malloc(sizeof(Client));
	temp_client->in = fdopen(*file_pointer, "r");
	temp_client->out = fdopen(*file_pointer, "w");

	//print room entry items
	fputs("Chat Rooms:\n\n", temp_client->out);
	fflush(temp_client->out);
	//print rooms and error catch
	jrb_traverse(temp_jrb, rooms) {
		fputs(temp_jrb->key.s, temp_client->out);
		fflush(temp_client->out);
		fputs(":", temp_client->out);
		fflush(temp_client->out);
		temp_room = (Room*)temp_jrb->val.v;
		dll_traverse(client_list, temp_room->clients) {
			fputs(" ", temp_client->out);
			fflush(temp_client->out);
			print_client = (Client*)client_list->val.v;
			fputs(print_client->name, temp_client->out);
			fflush(temp_client->out);
		}
		fputs("\n", temp_client->out);
		fflush(temp_client->out);
	}

	//get name and error catch
	fputs("\nEnter your chat name (no spaces):\n", temp_client->out);
	fflush(temp_client->out);
	if ((fgets(client_name, 256, temp_client->in)) == NULL) {
		printf("Error stuff here\n");
		fclose(temp_client->in);
		fclose(temp_client->out);
		pthread_detach(pthread_self());
		return NULL;
	}

	//add name to client
	client_name[strlen(client_name) - 1] = '\0';
	temp_client->name = strdup(client_name);

	//get room and error catch step one
	fputs("Enter chat room:\n", temp_client->out);
	fflush(temp_client->out);
	if ((fgets(room_name, 256, temp_client->in)) == NULL) {
		printf("Error stuff here\n");
		fclose(temp_client->in);
		fclose(temp_client->out);
		pthread_detach(pthread_self());
		return NULL;
	}
	room_name[strlen(room_name) - 1] = '\0';

	//get room and error catch for user reentry
	temp_jrb = jrb_find_str(rooms, room_name);
	while (temp_jrb == NULL) {
		fputs("No chat room ", temp_client->out);
		fflush(temp_client->out);
		fputs(room_name, temp_client->out);
		fflush(temp_client->out);
		fputs(".\nEnter chat room:\n", temp_client->out);
		fflush(temp_client->out);

		if ((fgets(room_name, 256, temp_client->in)) == NULL) {
			printf("Error stuff here\n");
			fclose(temp_client->in);
			fclose(temp_client->out);
			pthread_detach(pthread_self());
			return NULL;
		}
		room_name[strlen(room_name) - 1] = '\0';
		temp_jrb = jrb_find_str(rooms, room_name);
	}

	//add user to room and print out user joing msg
	temp_room = (Room*)(temp_jrb->val.v);
	message = malloc(sizeof(char) * (20 + strlen(client_name)));
	strcpy(message, client_name);
	strcat(message, " has joined\n");
	pthread_mutex_lock(temp_room->lock);
	dll_append(temp_room->clients, new_jval_v((void*)temp_client));
	dll_append(temp_room->msgs, new_jval_s(strdup(message)));
	free(message);
	pthread_cond_signal(temp_room->wait);
	pthread_mutex_unlock(temp_room->lock);

	//get input from user
	while (fgets(input, 1000, temp_client->in) != NULL) {
		if (strlen(input) <= 2) continue;
		pthread_mutex_lock(temp_room->lock);
		message = malloc(sizeof(char) * (strlen(client_name) + strlen(input) + 3));
		strcpy(message, client_name);
		strcat(message, ": ");
		strcat(message, input);
		dll_append(temp_room->msgs, new_jval_s(strdup(message)));
		free(message);
		pthread_cond_signal(temp_room->wait);
		pthread_mutex_unlock(temp_room->lock);
	}

	//close in stream
	fclose(temp_client->in);

	//if out is not closed print out the leaving message and delete client from list
	if (!feof(temp_client->out)) {
		fclose(temp_client->out);
		message = malloc(sizeof(char) * (11 + strlen(client_name)));
		strcpy(message, client_name);
		strcat(message, " has left\n");
		pthread_mutex_lock(temp_room->lock);
		dll_append(temp_room->msgs, new_jval_s(strdup(message)));
		dll_traverse(client_list, temp_room->clients) {
			temp_client = (Client*)client_list->val.v;
			if (strcmp(temp_client->name, client_name) == 0) {
				delete_client = client_list;
			}
		}
		dll_delete_node(delete_client);
		free(message);
		pthread_cond_signal(temp_room->wait);
		pthread_mutex_unlock(temp_room->lock);
	}
	//exit thread
	pthread_detach(pthread_self());
	return NULL;
}

int main(int argc, char** argv) {
	//varibles
	int i;
	int port;
	char* server;
	int socket;
	int fd;
	int* file_pointer;
	pthread_t* tid_rooms;
	pthread_t* tid_clients;

	//arg check and error catch
	if (argc < 3) {
		printf("usage: chat-server port Chat-Room-Names ...\n");
		exit(1);
	}

	//get port number and error catch
	port = atoi(argv[1]);
	if (port <= 8000) {
		printf("usage: chat-server port Chat-Room-Names ...\n");
		printf("port must be >= 8000.\n\n");
		exit(1);
	}

	//get rooms and process room threads
	rooms = make_jrb();
	for (i = 2; i < argc; i++) {
		Room* room_node;
		room_node = malloc(sizeof(Room));
		room_node->name = strdup(argv[i]);
		room_node->clients = new_dllist();
		room_node->msgs = new_dllist();
		room_node->lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
		room_node->wait = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
		pthread_mutex_init(room_node->lock, NULL);
		pthread_cond_init(room_node->wait, NULL);
		jrb_insert_str(rooms, argv[i], new_jval_v((void*)room_node));
		tid_rooms = (pthread_t*)malloc(sizeof(pthread_t));
		pthread_create(tid_rooms, NULL, process_rooms, room_node);
	}

	//serve socket and process client threads
	socket = serve_socket(port);
	while (1) {
		fd = accept_connection(socket);
		file_pointer = (int*)malloc(sizeof(int));
		*file_pointer = fd;
		tid_clients = (pthread_t*)malloc(sizeof(pthread_t));
		pthread_create(tid_clients, NULL, process_clients, file_pointer);
	}
	pthread_exit(NULL);
}