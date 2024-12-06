#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#define BUFFER_SIZE 819200

void handle_request(int client_socket);
void handle_get_request(int client_socket, const char *filename);
void handle_post_request(int client_socket, const char *filename, const char *data, int data_length);

#endif
