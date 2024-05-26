#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define MAX_WORDS 100

typedef struct
{
    char word_name[20];
    char word_definition[256];
} dictionary;

dictionary *dict;
int *word_count;
bool *initialized;

void add_word(const char *word);
void add_definition(const char *word, const char *definition);
void delete_word(const char *word);
void print_dictionary();
void initialize_dictionary();

void bzero(void *a, size_t n)
{
    memset(a, 0, n);
}

void bcopy(const void *src, void *dest, size_t n)
{
    memmove(dest, src, n);
}

struct sockaddr_in *init_sockaddr_in(uint16_t port_number)
{
    struct sockaddr_in *socket_address = malloc(sizeof(struct sockaddr_in));
    memset(socket_address, 0, sizeof(*socket_address));
    socket_address->sin_family = AF_INET;
    socket_address->sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address->sin_port = htons(port_number);
    return socket_address;
}

char *process_operation(char *input)
{
    size_t n = strlen(input) * sizeof(char);
    char *output = malloc(n);
    memcpy(output, input, n);
    return output;
}

int main(int argc, char *argv[])
{
    const uint16_t port_number = 5001;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in *server_sockaddr = init_sockaddr_in(port_number);
    struct sockaddr_in *client_sockaddr = malloc(sizeof(struct sockaddr_in));
    socklen_t server_socklen = sizeof(*server_sockaddr);
    socklen_t client_socklen = sizeof(*client_sockaddr);

    if (bind(server_fd, (const struct sockaddr *)server_sockaddr, server_socklen) < 0)
    {
        printf("Error! Bind has failed\n");
        exit(0);
    }
    if (listen(server_fd, 3) < 0)
    {
        printf("Error! Can't listen\n");
        exit(0);
    }

    // Create shared memory for dictionary, word_count, and initialized flag
    int shm_id_dict = shmget(IPC_PRIVATE, MAX_WORDS * sizeof(dictionary), IPC_CREAT | 0666);
    int shm_id_word_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_id_initialized = shmget(IPC_PRIVATE, sizeof(bool), IPC_CREAT | 0666);

    if (shm_id_dict == -1 || shm_id_word_count == -1 || shm_id_initialized == -1)
    {
        perror("shmget failed");
        exit(1);
    }

    // Attach shared memory
    dict = (dictionary *)shmat(shm_id_dict, NULL, 0);
    word_count = (int *)shmat(shm_id_word_count, NULL, 0);
    initialized = (bool *)shmat(shm_id_initialized, NULL, 0);

    if (dict == (dictionary *)-1 || word_count == (int *)-1 || initialized == (bool *)-1)
    {
        perror("shmat failed");
        exit(1);
    }

    // Initialize shared memory variables
    *word_count = 0;
    *initialized = false;
    memset(dict, 0, MAX_WORDS * sizeof(dictionary));

    const size_t buffer_len = 256;
    char *buffer = malloc(buffer_len * sizeof(char));
    char *response = NULL;
    time_t last_operation;
    __pid_t pid = -1;

    while (1)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_sockaddr, &client_socklen);

        pid = fork();

        if (pid == 0)
        {
            close(server_fd);

            if (client_fd == -1)
            {
                exit(0);
            }

            printf("Connection with `%d` has been established and delegated to the process %d.\nWaiting for a query...\n", client_fd, getpid());

            last_operation = clock();

            while (1)
            {
                read(client_fd, buffer, buffer_len);

                if (strcmp(buffer, "close") == 0)
                {
                    printf("Process %d: ", getpid());
                    close(client_fd);
                    printf("Closing session with `%d`. Bye!\n", client_fd);
                    break;
                }

                if (strlen(buffer) == 0)
                {
                    clock_t d = clock() - last_operation;
                    double dif = 1.0 * d / CLOCKS_PER_SEC;

                    if (dif > 5.0)
                    {
                        printf("Process %d: ", getpid());
                        close(client_fd);
                        printf("Connection timed out after %.3lf seconds. ", dif);
                        printf("Closing session with `%d`. Bye!\n", client_fd);
                        break;
                    }

                    continue;
                }

                printf("Process %d: Received `%s`. Processing...\n", getpid(), buffer);
                char response_message[512];
                char command = buffer[0];
                char rest_of_input[300];
                char definition[300];

                if (strlen(buffer) > 2 && buffer[1] == ' ')
                {
                    strncpy(rest_of_input, buffer + 2, sizeof(rest_of_input) - 1);
                    rest_of_input[sizeof(rest_of_input) - 1] = '\0';
                }
                else
                {
                    rest_of_input[0] = '\0';
                }

                switch (command)
                {
                case 'a':
                case 'A':
                    if (*initialized)
                    {
                        printf("%s", rest_of_input);
                        add_word(rest_of_input);
                    }
                    else
                    {
                        char response_message[512];
                        snprintf(response_message, sizeof(response_message), "Dictionary not initialized!");
                        send(client_fd, response_message, strlen(response_message), 0);
                    }
                    break;
                case 'd':
                    if (*initialized)
                    {
                        printf("Enter definition for %s: ", rest_of_input);
                        send(client_fd, "Enter your definition:\n", 24, 0); // Ask for the definition
                        bzero(buffer, buffer_len);
                        read(client_fd, buffer, buffer_len);   // Read the definition from client
                        add_definition(rest_of_input, buffer); // Add the definition

                        snprintf(response_message, sizeof(response_message), "Definition for '%s' added successfully.", rest_of_input);
                        send(client_fd, response_message, strlen(response_message), 0); // Send confirmation message back
                    }
                    else
                    {

                        snprintf(response_message, sizeof(response_message), "Dictionary not initialized!");
                        send(client_fd, response_message, strlen(response_message), 0);
                    }
                    break;

                case 's':
                    if (*initialized)
                    {
                        delete_word(rest_of_input);
                    }
                    else
                    {
                        snprintf(response_message, sizeof(response_message), "Dictionary not initialized!");
                        send(client_fd, response_message, strlen(response_message), 0);
                    }
                    break;
                case 'i':
                    if (!*initialized)
                    {
                        snprintf(response_message, sizeof(response_message), "Dictionary initialized\n");
                        send(client_fd, response_message, strlen(response_message), 0);
                        *initialized = true;
                        initialize_dictionary();
                    }
                    else
                    {
                        snprintf(response_message, sizeof(response_message), "Dictionary reinitialized\n");
                        send(client_fd, response_message, strlen(response_message), 0);
                        initialize_dictionary();
                    }
                    break;
                case 'p':
                    if (*initialized)
                    {
                        print_dictionary();
                    }
                    else
                    {
                        snprintf(response_message, sizeof(response_message), "Dictionary not initialized!");
                        send(client_fd, response_message, strlen(response_message), 0);
                    }
                    break;
                default:
                    printf(" UNKNOWN COMMAND\n");
                }

                free(response);
                response = process_operation(buffer);
                bzero(buffer, buffer_len * sizeof(char));

                send(client_fd, response, strlen(response), 0);
                printf(" Responded with `%s`. Waiting for a new query...\n", response);

                last_operation = clock();
            }
            exit(0);
        }
        else
        {
            close(client_fd);
        }
    }
}

void add_word(const char *word)
{
    for (int i = 0; i < *word_count; i++)
    {
        if (strcmp(dict[i].word_name, word) == 0)
        {
            printf(" Word already exists\n");
            return;
        }
    }
    strncpy(dict[*word_count].word_name, word, sizeof(dict[*word_count].word_name));
    dict[*word_count].word_definition[0] = '\0';
    (*word_count)++;
    printf(" Word added!\n");
}

void add_definition(const char *word, const char *definition)
{
    for (int i = 0; i < *word_count; i++)
    {
        if (strcmp(dict[i].word_name, word) == 0)
        {
            memset(dict[i].word_definition, 0, sizeof(dict[i].word_definition));
            strncpy(dict[i].word_definition, definition, sizeof(dict[i].word_definition) - 1);
            dict[i].word_definition[sizeof(dict[i].word_definition) - 1] = '\0';
            printf(" Definition added.\n");
            return;
        }
    }
    printf(" Word not found.\n");
}

void delete_word(const char *word)
{
    for (int i = 0; i < *word_count; i++)
    {
        if (strcmp(dict[i].word_name, word) == 0)
        {
            for (int j = i; j < *word_count - 1; j++)
            {
                dict[j] = dict[j + 1];
            }
            (*word_count)--;
            printf(" Word deleted.\n");
            return;
        }
    }
    printf(" Word not found.\n");
}

void print_dictionary()
{
    for (int i = 0; i < *word_count; i++)
    {
        printf(" Word: %s\nDefinition: %s\n", dict[i].word_name, dict[i].word_definition);
    }
}

void initialize_dictionary()
{
    memset(dict, 0, MAX_WORDS * sizeof(dictionary));
    *word_count = 0;
}
