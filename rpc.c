#include "rpc.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#define NONBLOCKING

int create_listening_socket(char* service);
int listen_for_connection(int sockfd);
void initialization(char* service, rpc_server* server);
void response(int sockfd, char* response);
void receive(int sockfd, char* buffer,size_t size);
rpc_handler search_for_handler(rpc_server* server, char* name);
rpc_data receive_data2(int socket,size_t size);
void send_data_server(int socket, rpc_data data);
int send_data_client(int socket,rpc_data data,char * name);
rpc_data* receive_data_client(int socket);
void *request_handler(void* thread);
void *search_request_handler(void* thread);
void *calculate_request_handler(void* thread);

typedef struct handler handler_t;
struct handler {
	char* name;
	rpc_handler handler;
};

typedef struct handler_list handler_list_t;
struct handler_list {
	handler_t** handlers;
	int size;
};

struct rpc_server {
    int listening_socket;
	handler_list_t* handler_list;
};

typedef struct rpc_thread {
	rpc_server* srv;
	int sockfd;
} rpc_thread_t;

rpc_server *rpc_init_server(int port) {
	//Initialize server
	rpc_server *server = malloc(sizeof(rpc_server));
	char port_num[5];
	sprintf(port_num, "%d", port);
	initialization(port_num, server);
	if (server->listening_socket < 0){
		return NULL;
	}
	server->handler_list = malloc(sizeof(handler_list_t));
	server->handler_list->size = 0;
	server->handler_list->handlers = malloc(sizeof(handler_t*)*20);
    return server;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
	//Register function handler
	if (srv == NULL || name == NULL || handler == NULL){
		return -1;
	}
	if (srv->handler_list->size == 10) {
		return -1;
	}
	for (int i = 0; i < srv->handler_list->size; i++){
		if (strcmp(srv->handler_list->handlers[i]->name, name) == 0){
			srv->handler_list->handlers[i]->handler = handler;
			return 1;
		}
	}
	handler_t* new_handler = malloc(sizeof(handler_t));
	new_handler->name = malloc(sizeof(char)*1000);
	strcpy(new_handler->name, name);
	new_handler->handler = handler;
	srv->handler_list->size++;
	srv->handler_list->handlers[srv->handler_list->size-1] = new_handler;
    return 1;
}

void rpc_serve_all(rpc_server *srv) {
	//Start to serve
	if (srv == NULL){
		return;
	}

	if (listen(srv->listening_socket, 5) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	while (1){
		//Create a thread to handle request for each incoming connection
		int newsockfd = listen_for_connection(srv->listening_socket);
		pthread_t tid;
		rpc_thread_t* thread = malloc(sizeof(rpc_thread_t));
		thread->srv = srv;
		thread->sockfd = newsockfd;
		pthread_create(&tid, NULL, request_handler, (void*)thread);
	}
}
//------------------------------------------------------------------------------------
struct rpc_client {
    int sockfd;
};

struct rpc_handle {
    char* name;
};

rpc_client *rpc_init_client(char *addr, int port) {
	//Initialize client
	int sockfd, s;
	struct addrinfo hints, *servinfo, *rp;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	char port_num[5];
	sprintf(port_num, "%d", port);
	s = getaddrinfo(addr, port_num, &hints, &servinfo);
	if (s != 0) {
		return NULL;
	}

	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sockfd == -1)
			continue;

		if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break; 

		close(sockfd);
	}
	if (rp == NULL) {
		return NULL;
	}
	freeaddrinfo(servinfo);
	rpc_client* client = malloc(sizeof(rpc_client));
	client->sockfd = sockfd;
    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
	//Find function handler in server
	if (cl == NULL || name == NULL){
		return NULL;
	}
	if (strlen(name) > 1000){
		return NULL;
	}
	char buffer[1600];
	memset(buffer, 0, 1600);
	sprintf(buffer, "200 %s ", name);
	response(cl->sockfd, buffer);
	receive(cl->sockfd, buffer, 1600);
	char* token = strtok(buffer, " ");
	if (strcmp(token, "201")==0){
		return NULL;
	}
	rpc_handle* handle = malloc(sizeof(rpc_handle));
	handle->name = malloc(strlen(name) + 1);
	strcpy(handle->name, name);
    return handle;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
	//Call function handler in server
	if (cl == NULL || h == NULL || payload == NULL){
		return NULL;
	}
	if (payload->data2==NULL && payload->data2_len!=0){
		return NULL;
	}
	if (payload->data2!=NULL && payload->data2_len==0){
		return NULL;
	}
	int n = send_data_client(cl->sockfd, *payload, h->name);
	if (n < 0){
		if (n == -2){
			fprintf(stderr, "Error: function name not found\n");
		}
		return NULL;
	}
	rpc_data* result = receive_data_client(cl->sockfd);
	if (result == NULL){
		return NULL;
	}
	return result;
}

void rpc_close_client(rpc_client *cl) {
	if (cl == NULL) {
		return;
	}
	close(cl->sockfd);
	free(cl);
	cl = NULL;
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}


//-------------------------------------------------------------------------------------------------------


void initialization(char* service, rpc_server* server) {
	//initialize the rpc_server by creating an listening socket
	int sockfd;
	sockfd = create_listening_socket(service);
	server->listening_socket = sockfd;
}

int create_listening_socket(char* service) {
	//Create a listening socket (Mostly from tutorial week9)
	int re, s, sockfd;
	struct addrinfo hints, *res;

	// Create address we're going to listen on (with given port number)
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;       
	hints.ai_socktype = SOCK_STREAM; 
	hints.ai_flags = AI_PASSIVE;     
	s = getaddrinfo(NULL, service, &hints, &res);
	if (s != 0) {
		return -1;
	}

	for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
		if (p->ai_family == AF_INET6 ) {
			sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
			if (sockfd < 0) {
				return -1;
			}
		}
	}

	// Reuse port if possible
	re = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
		return -1;
	}
	// Bind address to the socket
	if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
		return -1;
	}
	freeaddrinfo(res);

	return sockfd;
}

int listen_for_connection(int sockfd){
	//Listen for and accept a connection from client
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	client_addr_size = sizeof client_addr;
	int newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
	if (newsockfd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	return newsockfd;
}


void response(int sockfd, char* response) {
	//Send response to the server/client
	int n;
	n = write(sockfd, response, strlen(response));
	if (n < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
}

void receive(int sockfd, char* buffer,size_t size) {
	//Receive response from the server/client
	int n;
	n = read(sockfd, buffer, size);
	if (n < 0) {
		perror("read");
		exit(EXIT_FAILURE);
	}
}

rpc_handler search_for_handler(rpc_server* server, char* name) {
	//Search for the handler in the handler_list
	for (int i = 0;i<server->handler_list->size;i++) {
		if (strcmp(server->handler_list->handlers[i]->name, name) == 0) {
			return server->handler_list->handlers[i]->handler;
		}
	}
	return NULL;
}

rpc_data receive_data2(int socket,size_t size) {
	//Receive data2 from the server/client
	rpc_data data;
	data.data2_len = size;
	if (size>0){
		char data_buffer[size];
		read(socket, data_buffer, sizeof(data_buffer));
		data.data2 = (void*)malloc(size);
		memcpy(data.data2, data_buffer, size);
	}else{
		data.data2 = NULL;
	}
	return data;
}

void send_data_server(int socket, rpc_data data) {
	//Send data from server to the client
	char buffer[1600];
	memset(buffer, 0, 1600);
	sprintf(buffer, "%d ", 400);
	response(socket, buffer);
	receive(socket, buffer, 1600);
	char* instruction = strtok(buffer, " ");
	if (strcmp(instruction, "400") != 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	
	uint64_t size = htonl(data.data2_len);
	uint64_t data1 = (uint64_t)data.data1;
	data1 = htobe64(data.data1);

	write(socket, &size, sizeof(uint64_t));
	receive(socket, buffer, 1600);
	instruction = strtok(buffer, " ");
	if (strcmp(instruction, "400") != 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	write(socket, &data1, sizeof(uint64_t));
	memset(buffer, 0, 1600);
	receive(socket, buffer, 1600);
	instruction = strtok(buffer, " ");
	if (strcmp(instruction, "400") != 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}


	if (data.data2_len == 0) {
		return;
	}
	char buffer2[data.data2_len];
	memcpy(buffer2, data.data2, data.data2_len);
	int n = write(socket, buffer2, data.data2_len);
	if (n < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
}

int send_data_client(int socket,rpc_data data,char * name){
	//Send data from client to the server
	if (data.data1>pow(2,64)-1){
		return -1;
	}
	char buffer[1600];
	memset(buffer, 0, 1600);
	sprintf(buffer, "%d %s ", 300, name);
	response(socket, buffer);
	receive(socket, buffer, 1600);
	char* instruction = strtok(buffer, " ");
	if (strcmp(instruction, "300") != 0) {
		return -2;
	}
	
	uint64_t size = htonl(data.data2_len);
	uint64_t data1 = (uint64_t)data.data1;
	data1 = htobe64(data.data1);
	write(socket, &size, sizeof(uint64_t));
	receive(socket, buffer, 1600);
	instruction = strtok(buffer, " ");
	if (strcmp(instruction, "300") != 0) {
		return -1;
	}
	write(socket, &data1, sizeof(uint64_t));
	memset(buffer, 0, 1600);
	receive(socket, buffer, 1600);
	instruction = strtok(buffer, " ");
	if (strcmp(instruction, "300") != 0) {
		return -1;
	}

	if (data.data2_len == 0) {
		return 1;
	}
	char buffer2[data.data2_len];
	memcpy(buffer2, data.data2, data.data2_len);
	int n = write(socket, buffer2, data.data2_len);
	if (n < 0) {
		return -1;
	}
	return 1;
}

rpc_data* receive_data_client(int socket){
	//Receive data from the server
	char buffer[1600];
	char res[1600];
	memset(buffer, 0, 1600);
	receive(socket, buffer, 1600);
	char* instruction = strtok(buffer, " ");
	if (strcmp(instruction, "400") != 0) {
		return NULL;
	}

	sprintf(res, "%d ", 400);
	response(socket, res);
	uint64_t size_uint64;
	uint64_t data1_uint64;
	read(socket, &size_uint64, sizeof(uint64_t));
	response(socket, res);
	read(socket, &data1_uint64, sizeof(uint64_t));
	int data2_len = ntohl(size_uint64);
	data1_uint64 = be64toh(data1_uint64);
	int data1 = (int)data1_uint64;
	response(socket, res);

	rpc_data* data;
	rpc_data rp= receive_data2(socket, data2_len);
	data = (rpc_data*)malloc(sizeof(rpc_data));
	data->data2 = rp.data2;
	data->data2_len = rp.data2_len;
	data->data1 = data1;
	return data;
}


void *request_handler(void* thread){
	//Handle the request from the client
	char buffer[1600];
	char res[1600];
	char* instruction;
	char* name;
	int newsockfd = ((rpc_thread_t*)thread)->sockfd;
	rpc_server* srv = ((rpc_thread_t*)thread)->srv;
	free(thread);

	while (1){
		memset(buffer, 0, 1600);
		memset(res, 0, 1600);
		receive(newsockfd, buffer, 1600);

		if (buffer[0] == '\0'){
			continue;
		}
		char* token = strtok(buffer, " ");
		instruction=token;
		token = strtok(NULL, " ");
		name = token;

		if (strcmp(instruction, "200")==0){
			//Handle rpc-find request
			if (search_for_handler(srv, name) == NULL){
				sprintf(res, "201 ");
				response(newsockfd, res);
			} else{
				sprintf(res, "200 ");
				response(newsockfd, res);
			}
		}else if (strcmp(instruction, "300")==0){
			//Handle rpc-call request
			sprintf(res, "300 ");
			response(newsockfd, res);
			uint64_t size;
			uint64_t data1;
			receive(newsockfd, (char*)&size, sizeof(uint64_t));
			response(newsockfd, res);
			receive(newsockfd, (char*)&data1, sizeof(uint64_t));
			size_t data_2_size = ntohl(size);
			data1 = be64toh(data1);
			int data_1 = (int)data1;

			rpc_handler handler = search_for_handler(srv, name);
			if (handler == NULL){
				sprintf(res, "301 ");
				response(newsockfd, res);
				continue;
			}
			sprintf(res, "300 ");
			response(newsockfd, res);

			rpc_data data = receive_data2(newsockfd, data_2_size);
			data.data1 = data_1;
			rpc_data* result = handler(&data);
			if (result==NULL){
				sprintf(res, "401 ");
				response(newsockfd, res);
				continue;
			}
			if (result->data2==NULL && result->data2_len!=0){
				sprintf(res, "401 ");
				response(newsockfd, res);
				continue;
			}
			if (result->data2!=NULL && result->data2_len==0){
				sprintf(res, "401 ");
				response(newsockfd, res);
				continue;
			}
			send_data_server(newsockfd, *result);
			rpc_data_free(result);
			if (data.data2 != NULL){
				free(data.data2);
			}
		}
	}
	return NULL;
}