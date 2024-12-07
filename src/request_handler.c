#include "request_handler.h"
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "logger.h"

extern sem_t file_semaphore;

#define SECRET_KEY "K5HS4KzQiL"
#define MAX_HEADER_SIZE 32768 // Maximum allowed header size
#define MAX_LINE_SIZE 4096    // Maximum allowed size for a header line

void handle_request(int client_socket)
{
    char buffer[BUFFER_SIZE];
    int total_read = 0;
    int bytes_read;
    int header_end = -1;

    memset(buffer, 0, BUFFER_SIZE); // Clear the buffer

    // Read headers
    while ((bytes_read = read(client_socket, buffer + total_read, BUFFER_SIZE - total_read - 1)) > 0)
    {
        total_read += bytes_read;
        buffer[total_read] = '\0';

        // Check if we have reached the end of headers (\r\n\r\n or \n\n)
        char *end_of_headers = strstr(buffer, "\r\n\r\n");
        if (end_of_headers != NULL)
        {
            header_end = end_of_headers - buffer + 4;
            break;
        }
        else
        {
            end_of_headers = strstr(buffer, "\n\n");
            if (end_of_headers != NULL)
            {
                header_end = end_of_headers - buffer + 2;
                break;
            }
        }

        // Prevent buffer overflow
        if (total_read >= MAX_HEADER_SIZE)
        {
            log_message(LOG_ERROR, "Headers too large");
            write(client_socket, "HTTP/1.1 413 Payload Too Large\r\n\r\n", 35);
            return;
        }
    }

    if (header_end == -1)
    {
        // Did not find end of headers
        log_message(LOG_ERROR, "Invalid HTTP request: End of headers not found");
        write(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
        return;
    }

    // Parse the method and path
    char method[16], path[256];
    sscanf(buffer, "%15s %255s", method, path);

    // Parse headers to find Content-Length and the "secret" parameter
    int content_length = 0;
    char secret[256] = {0}; // Buffer to store the secret value if found
    char *current_line = buffer;
    while (current_line < buffer + header_end)
    {
        // Find the end of the current line
        char *next_line = strstr(current_line, "\r\n");
        int line_ending_length = 2;
        if (next_line == NULL || next_line >= buffer + header_end)
        {
            next_line = strstr(current_line, "\n");
            line_ending_length = 1;
            if (next_line == NULL || next_line >= buffer + header_end)
            {
                break;
            }
        }

        // Calculate line length and copy it
        int line_length = next_line - current_line;
        if (line_length >= MAX_LINE_SIZE)
        {
            log_message(LOG_ERROR, "Header line too long");
            write(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
            return;
        }
        char line[MAX_LINE_SIZE];
        memcpy(line, current_line, line_length);
        line[line_length] = '\0';

        // Parse Content-Length header
        if (strncasecmp(line, "Content-Length:", 15) == 0)
        {
            char *cl = line + 15;
            while (*cl == ' ' || *cl == '\t')
                cl++; // Skip whitespace
            content_length = atoi(cl);
        }

        // Parse X-Secret header
        if (strncasecmp(line, "X-Secret:", 9) == 0)
        {
            char *s = line + 9;
            while (*s == ' ' || *s == '\t')
                s++; // Skip whitespace
            strncpy(secret, s, sizeof(secret) - 1);
            secret[sizeof(secret) - 1] = '\0'; // Ensure null termination
        }

        // Move to the next line
        current_line = next_line + line_ending_length;
    }

    // Validate the "secret" parameter
    if (strcmp(secret, SECRET_KEY) != 0)
    {
        log_message(LOG_ERROR, "Invalid secret");
        write(client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 27);
        return;
    }

    // Read the body if present
    char *body = buffer + header_end;
    int body_read = total_read - header_end;

    if (content_length > 0)
    {
        if (content_length > BUFFER_SIZE)
        {
            log_message(LOG_WARNING, "Allocating memory for large payload");
            body = malloc(content_length);
            if (!body)
            {
                log_message(LOG_ERROR, "Failed to allocate memory for payload");
                write(client_socket, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 39);
                return;
            }
            memcpy(body, buffer + header_end, body_read);
            int remaining = content_length - body_read;

            while (remaining > 0)
            {
                bytes_read = read(client_socket, body + body_read, remaining);
                if (bytes_read <= 0)
                {
                    log_message(LOG_ERROR, "Failed to read full request body");
                    free(body);
                    write(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
                    return;
                }
                body_read += bytes_read;
                remaining -= bytes_read;
            }
        }
        else
        {
            body = buffer + header_end;
        }

        // Read the remaining body data
        while (body_read < content_length)
        {
            bytes_read = read(client_socket, buffer + total_read, BUFFER_SIZE - total_read - 1);
            if (bytes_read <= 0)
            {
                log_message(LOG_ERROR, "Failed to read request body");
                write(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
                return;
            }
            total_read += bytes_read;
            body_read += bytes_read;
            buffer[total_read] = '\0';
        }
    }

    // Process the request based on the method
    if (strcmp(method, "GET") == 0)
    {
        handle_get_request(client_socket, path + 1); // Skip the leading '/'
    }
    else if (strcmp(method, "POST") == 0)
    {
        handle_post_request(client_socket, path + 1, body, content_length);
        if (content_length > BUFFER_SIZE)
        {
            free((void *)body); // Free the dynamically allocated memory
        }
    }
    else
    {
        write(client_socket, "HTTP/1.1 405 Method Not Allowed\r\n\r\n", 35);
    }
}
void handle_get_request(int client_socket, const char *filename)
{
    log_message(LOG_INFO, "GET request for file: %s", filename);
    sem_wait(&file_semaphore);

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        log_message(LOG_ERROR, "File not found: %s", filename);
        write(client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", 26);
    }
    else
    {
        log_message(LOG_INFO, "File opened successfully: %s", filename);
        write(client_socket, "HTTP/1.1 200 OK\r\n\r\n", 19);

        char buffer[BUFFER_SIZE];
        ssize_t bytes;
        while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0)
        {
            ssize_t total_written = 0;
            while (total_written < bytes)
            {
                ssize_t written = write(client_socket, buffer + total_written, bytes - total_written);
                if (written <= 0)
                {
                    log_message(LOG_ERROR, "Failed to send file content");
                    break;
                }
                total_written += written;
            }
        }
        close(fd);
        log_message(LOG_INFO, "File sent successfully: %s", filename);
    }

    sem_post(&file_semaphore);
}

void handle_post_request(int client_socket, const char *filename, const char *data, int data_length)
{
    log_message(LOG_INFO, "POST request for file: %s", filename);

    sem_wait(&file_semaphore);

    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        log_message(LOG_ERROR, "Failed to open file for writing: %s", filename);
        write(client_socket, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 39);
    }
    else
    {
        log_message(LOG_INFO, "File opened for writing: %s", filename);

        ssize_t total_written = 0;
        while (total_written < data_length)
        {
            ssize_t written = write(fd, data + total_written, data_length - total_written);
            if (written <= 0)
            {
                log_message(LOG_ERROR, "Failed to write to file: %s", filename);
                write(client_socket, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 39);
                close(fd);
                sem_post(&file_semaphore);
                return;
            }
            total_written += written;
        }

        log_message(LOG_INFO, "Data written successfully to file: %s", filename);
        close(fd);
        write(client_socket, "HTTP/1.1 200 OK\r\n\r\n", 19);
    }

    sem_post(&file_semaphore);
}
