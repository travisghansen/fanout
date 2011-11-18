/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   https://github.com/hbons/fanout.node.js/blob/master/fanout.js*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>


fd_set readset, tempset;

int max;
int subscription_count = 0;

struct client
{
    int fd;
    char *input_buffer;
    char *output_buffer;
    struct client *next;
    struct client *previous;
};


struct subscription
{
    int fd;
    char *channel;
    struct subscription *next;
    struct subscription *previous;
};

struct subscription *subscription_head = NULL;
struct subscription *subscription_i = NULL;

void fanout_error(const char *msg)
{
    perror(msg);
    exit (1);
}


void fanout_debug (const char *msg)
{
    printf("debug: %s\n", msg);
}


size_t easy_write (int fd, const char *s)
{
    return (write (fd, s, strlen (s)));
}

void append_nullbyte (char *s) {
    size_t len = strlen (s);
    s[len + 1] = '\0';
}

void subscribe (int fd, char *channel) {
    int len;
    len = sizeof (struct subscription);
    if ((subscription_i = malloc (len)) == NULL) {
        printf("Memory Error.\n");
        return;
    }
    printf ("subscribing client %d to channel %s\n", fd, channel);
    memset (subscription_i, 0, len);
    subscription_i->fd = fd;
    subscription_i->channel = channel;
    //subscription_i->channel[strlen (subscription_i->channel) + 1] = '\0';
    printf ("channel set to %s\n", subscription_i->channel);
    subscription_i->next = subscription_head;
    if (subscription_head != NULL)
        subscription_head->previous = subscription_i;
    subscription_head = subscription_i;
}

void announce (char *channel, char *message) {
    printf ("attempting to announce message %s to channel %s\n", message, channel);
    subscription_i = subscription_head;
    while (subscription_i != NULL) {
        printf ("testing subscription for client %d on channel %s\n", subscription_i->fd, subscription_i->channel);
        if (subscription_i->channel == channel) {
            printf ("announcing message %s to %d on channel %s\n", message, subscription_i->fd, channel);
        }
        subscription_i = subscription_i->next;
    }
}

int main(int argc, char *argv[])
{
    int srvsock, newsock, portno, res, len;
    int i;
    u_int yes = 1;
    char *line, *action, *channel, *message, *output;
    char buffer[1024];

    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    struct client *client_head = NULL;
    struct client *client_i = NULL;

    if (argc < 2)
        fanout_error ("ERROR, no port provided");
    
    srvsock = socket(AF_INET, SOCK_STREAM, 0);
    
    if (srvsock < 0)
        fanout_error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    
    setsockopt (srvsock,SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(srvsock, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0)
    {
        fanout_error("ERROR on binding");
    }

    listen(srvsock,5);
    
    max = srvsock;

    FD_SET(srvsock, &readset);

    while (1)
    {
        fanout_debug ("server waiting for new activity");
        
        //memcpy(&tempset, &readset, sizeof(readset));
        tempset = readset;
        // Wait indefinitely for read events 
        res = select (max+1, &tempset, NULL, NULL, NULL);

        if (res < 0)
        {
            printf ("something bad happened\n");
            continue;
        }

        if (res == 0)
        {
            printf ("this should happen when timeout is met, in our case never\n");
            continue;
        }

        // Process new connections first 
        if (FD_ISSET (srvsock, &tempset)) {
            len = sizeof (struct client);
            if ((client_i = malloc(len)) == NULL) {
                printf("Memory Error.\n");
                continue;
            }
            memset(client_i, 0, len);
            
            //Shove current new connection in the front of the line
            client_i->next = client_head;
            if (client_head != NULL) {
                client_head->previous = client_i;
            }
            client_head = client_i;

            len = sizeof(cli_addr);
            client_i->fd = accept(srvsock, (struct sockaddr*)&cli_addr, &len);
            FD_SET(client_i->fd, &readset);
            if (client_i->fd > max) {
                max = client_i->fd;
            }

            fanout_debug("New client socket connect");
            send (client_i->fd, "debug!connected...\n", strlen ("debug!connected...\n"), MSG_DONTWAIT);
       }

        // Process events of other sockets...
        client_i = client_head;
        while (client_i != NULL) {
            if (FD_ISSET (client_i->fd, &tempset)) {
                // Process data from socket i
                res = recv(client_head->fd, buffer, 1024, 0);
                if (res <= 0) {
                    fanout_debug ("client socket disconnected");

                    if (client_i->next != NULL)
                        client_i->next->previous = client_i->previous;
                    if (client_i->previous != NULL)
                        client_i->previous->next = client_i->next;

                    FD_CLR (client_i->fd, &readset);
                    shutdown (client_i->fd, 2);
                    close (client_i->fd);
                } else {
                    // Process data in buffer
                    printf ("%d bytes read: [%.*s]\n", res, (res - 1), buffer);
                    line = strtok (buffer, "\n");
                    if (line != NULL) {
                        if ( ! strcmp (line, "ping")) {
                            asprintf (&message, "%d\n", time(NULL));
                            send (client_i->fd, message, strlen (message), MSG_DONTWAIT);
                            free (message);
                            message = NULL;
                        } else {
                            action = strtok (line, " ");
                            channel = strtok (NULL, " ");
                            message = strtok (NULL, " ");
                            if (action == NULL) {
                                fanout_debug ("received garbage from client");
                                continue;
                            } else {
                                if ( ! strcmp (action, "announce")) {
                                    //perform announce
                                    announce (channel, message);
                                } else if ( ! strcmp (action, "subscribe")) {
                                    //perform subscribe
                                    //append_nullbyte (channel);
                                    subscribe (client_i->fd, channel);
                                } else if ( ! strcmp (action, "unsubscribe")) {
                                    //perform unsubscribe
                                } else {
                                    fanout_debug ("invalid action attempted");
                                }
                            }
                        }
                    } else {
                        fanout_debug ("received garbage from client");
                    }
                }
            }
            client_i = client_i->next;
        }
    }

    close(srvsock);
    return 0; 
}
