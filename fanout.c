/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   https://github.com/hbons/fanout.node.js/blob/master/fanout.js*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>


struct client
{
    int fd;
    char *input_buffer;
    char *output_buffer;
    struct client *next;
    struct client *previous;
};


struct channel
{
    char *name;
    struct channel *next;
    struct channel *previous;
    u_int subscription_count;
};

struct subscription
{
    struct client *client;
    struct channel *channel;
    struct subscription *next;
    struct subscription *previous;
};

int strcpos (const char *haystack, const char c);
char *substr (const char *s, int start, int stop);
void str_swap_free (char **target, char *source);
char *str_append (char *target, const char *data);
void fanout_error(const char *msg);
void fanout_debug (const char *msg);

int channel_exists (const char *channel_name);
int channel_has_subscription (struct channel *c);
struct channel *get_channel (const char *channel_name);
void remove_channel (struct channel *c);
void destroy_channel (struct channel *c);


struct client *get_client (int fd);
void remove_client (struct client *c);
void shutdown_client (struct client *c);
void destroy_client (struct client *c);
void client_write (struct client *c, const char *data);
void client_process_input_buffer (struct client *c);


struct subscription *get_subscription (struct client *c,
                                        struct channel *channel);
void remove_subscription (struct subscription *s);
void destroy_subscription (struct subscription *s);


void announce (const char *channel_name, const char *message);
void subscribe (struct client *c, const char *channel_name);
void unsubscribe (struct client *c, const char *channel_name);


// GLOBAL VARS
fd_set readset, tempset;
int max;
struct client *client_head = NULL;
struct subscription *subscription_head = NULL;
struct channel *channel_head = NULL;



int main(int argc, char *argv[])
{
    int srvsock, portno, res;
    u_int yes = 1;
    u_int listen_backlog = 25;
    char buffer[1025];

    struct client *client_i = NULL;
    struct client *client_tmp = NULL;

    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

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
    
    setsockopt (srvsock,SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
    if (bind(srvsock, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0)
    {
        fanout_error ("ERROR on binding");
    }

    if (listen (srvsock, listen_backlog) == -1)
        fanout_error ("ERROR listening on server socket");

    max = srvsock;

    FD_SET (srvsock, &readset);

    while (1) {
        fanout_debug ("server waiting for new activity");

        tempset = readset;
        // Wait indefinitely for read events 
        res = select (max+1, &tempset, NULL, NULL, NULL);

        if (res < 0) {
            printf ("something bad happened\n");
            continue;
        }

        if (res == 0) {
            printf ("timeout is met, in our case never\n");
            continue;
        }

        // Process new connections first 
        if (FD_ISSET (srvsock, &tempset)) {
            if ((client_i = calloc (1, sizeof (struct client))) == NULL) {
                fanout_debug ("memory error");
                continue;
            }

            //Shove current new connection in the front of the line
            client_i->next = client_head;
            if (client_head != NULL) {
                client_head->previous = client_i;
            }
            client_head = client_i;

            clilen = sizeof (cli_addr);
            client_i->fd = accept (srvsock, (struct sockaddr *)&cli_addr,
                                    &clilen);
            FD_SET (client_i->fd, &readset);
            if (client_i->fd > max) {
                max = client_i->fd;
            }

            fanout_debug ("client socket connected");
            client_write (client_i, "debug!connected...\n");
            subscribe (client_i, "all");
       }

        // Process events of other sockets...
        client_i = client_head;
        while (client_i != NULL) {
            if (FD_ISSET (client_i->fd, &tempset)) {
                // Process data from socket i
                printf ("processing client %d\n", client_i->fd);
                memset (buffer, 0, sizeof (buffer));
                res = recv (client_i->fd, buffer, 1024, 0);
                if (res <= 0) {
                    fanout_debug ("client socket disconnected");

                    client_tmp = client_i;
                    client_i = client_i->next;

                    shutdown_client (client_tmp);
                    continue;
                } else {
                    // Process data in buffer
                    printf ("%d bytes read: [%.*s]\n", res, (res - 1), buffer);
                    if (client_i->input_buffer != NULL) {
                        fanout_debug ("input buffer contains data");
                        client_i->input_buffer = str_append (
                                                client_i->input_buffer, buffer);
                    } else {
                        fanout_debug ("input buffer empty, intializing");
                        asprintf (&client_i->input_buffer, "%s", buffer);
                    }
                    client_process_input_buffer (client_i);
                }
            }
            client_i = client_i->next;
        }
    }

    close(srvsock);
    return 0; 
}



int strcpos (const char *haystack, const char c)
{
    int i;
    for (i = 0; i <= strlen (haystack); i++) {
        if (haystack[i] == c)
            return i;
    }
    return -1;
}


char *substr (const char *s, int start, int stop)
{
    char *v;
    asprintf(&v, "%.*s", stop - start + 1, &s[start]);
    return v;
}


void str_swap_free (char **target, char *source)
{
    free (*target);
    *target = source;
}


char *str_append (char *target, const char *data)
{
    int len = strlen (target) + strlen (data) + 1;
    target = realloc (target, len);

    return strcat (target, data);
}

void fanout_error(const char *msg)
{
    perror(msg);
    exit (1);
}


void fanout_debug (const char *msg)
{
    printf("[%d] DEBUG: %s\n", (u_int) time(NULL), msg);
}


int channel_exists (const char *channel_name)
{
    struct channel *channel_i = channel_head;
    while (channel_i != NULL) {
        if ( ! strcmp (channel_name, channel_i->name))
            return 1;
        channel_i = channel_i->next;
    }
    return 0;
}


int channel_has_subscription (struct channel *c)
{
    if (c->subscription_count > 0) {
        return 1;
    }
    return 0;
}


struct channel *get_channel (const char *channel_name)
{
    struct channel *channel_i = channel_head;

    while (channel_i != NULL) {
        if ( ! strcmp (channel_name, channel_i->name))
            return channel_i;
        channel_i = channel_i->next;
    }

    fanout_debug ("creating new channel");
    if ((channel_i = calloc (1, sizeof (struct channel))) == NULL) {
        fanout_error ("memory error");
    }

    asprintf (&channel_i->name, "%s", channel_name);
    channel_i->next = channel_head;
    if (channel_head != NULL)
        channel_head->previous = channel_i;
    channel_head = channel_i;
    return channel_i;
}


void remove_channel (struct channel *c)
{
    printf ("removing unused channel %s\n", c->name);
    if (c->next != NULL) {
        c->next->previous = c->previous;
    }
    if (c->previous != NULL) {
        c->previous->next = c->next;
    }
    if (c == channel_head) {
        channel_head = c->next;
    }
}


void destroy_channel (struct channel *c)
{
    free (c->name);
    free (c);
}


struct client *get_client (int fd)
{
    struct client *client_i = NULL;

    client_i = client_head;
    while (client_i != NULL) {
        if (client_i->fd == fd)
            return client_i;
        client_i = client_i->next;
    }
    fanout_error ("client does not exist");
}


void remove_client (struct client *c)
{
    printf ("removing client %d from service\n", c->fd);
    if (c->next != NULL) {
        c->next->previous = c->previous;
    }
    if (c->previous != NULL) {
        c->previous->next = c->next;
    }
    if (c == client_head) {
        client_head = c->next;
    }
}


void shutdown_client (struct client *c)
{
    struct subscription *subscription_i = subscription_head;

    while (subscription_i != NULL) {
        if (c == subscription_i->client) {
            remove_subscription (subscription_i);
            destroy_subscription (subscription_i);
        }
        subscription_i = subscription_i->next;
    }

    remove_client (c);
    FD_CLR (c->fd, &readset);
    shutdown (c->fd, 2);
    close (c->fd);
    destroy_client (c);
}


void destroy_client (struct client *c)
{
    free (c->input_buffer);
    free (c->output_buffer);
    free (c);
}


void client_write (struct client *c, const char *data)
{
    int sent;

    if (c->output_buffer != NULL) {
        fanout_debug ("output buffer contains data");
        c->output_buffer = str_append (c->output_buffer, data);
    } else {
        fanout_debug ("output buffer empty, intializing");
        asprintf (&c->output_buffer, "%s", data);
    }

    while (strlen (c->output_buffer) > 0) {
        sent = send (c->fd, c->output_buffer, strlen (c->output_buffer), 0);
        if (sent == -1)
            break;
        printf ("wrote %d bytes\n", sent);
        str_swap_free (&c->output_buffer, substr (c->output_buffer, sent,
                        strlen (c->output_buffer)));
    }
    printf ("remaining output buffer is %d chars: %s\n",
             (u_int) strlen (c->output_buffer), c->output_buffer);
}


void client_process_input_buffer (struct client *c)
{
    char *line;
    char *message;
    char *action;
    char *channel;

    int i;

    printf ("full buffer\n\n%s\n\n", c->input_buffer);
    while ((i = strcpos (c->input_buffer, '\n')) >= 0) {
        line = substr (c->input_buffer, 0, i -1);
        printf ("buffer has a newline at char %d\n", i);
        printf ("line is %d chars: %s\n", (u_int) strlen (line), line);

        if ( ! strcmp (line, "ping")) {
            asprintf (&message, "%d\n", (u_int) time(NULL));
            client_write (c, message);
            free (message);
            message = NULL;
        } else {
            action = strtok (line, " ");
            channel = strtok (NULL, " ");
            if (action == NULL) {
                fanout_debug ("received garbage from client");
            } else {
                if ( ! strcmp (action, "announce")) {
                    //perform announce
                    message = substr (line,
                                       strlen (action) + strlen (channel) + 2,
                                       strlen (line));
                    if (channel_exists (channel) && strlen (message) > 0)
                        announce (channel, message);
                    free (message);
                } else if ( ! strcmp (action, "subscribe")) {
                    //perform subscribe
                    if (strcpos (channel, '!') == -1)
                        subscribe (c, channel);
                } else if ( ! strcmp (action, "unsubscribe")) {
                    //perform unsubscribe
                    if (strcpos (channel, '!') == -1)
                        unsubscribe (c, channel);
                } else {
                    fanout_debug ("invalid action attempted");
                }
            }
        }

        str_swap_free (&c->input_buffer, substr (c->input_buffer, i + 1,
                                   strlen (c->input_buffer)));
        free (line);
    }

    printf ("remaining input buffer is %d chars: %s\n",
             (int) strlen (c->input_buffer), c->input_buffer);
}


struct subscription *get_subscription (struct client *c,
                                        struct channel *channel)
{
    struct subscription *subscription_i = subscription_head;
    while (subscription_i != NULL) {
        if (subscription_i->channel == channel && c == subscription_i->client)
            return subscription_i;
        subscription_i = subscription_i->next;
    }
}


void remove_subscription (struct subscription *s)
{
    printf ("unsubscribing client %d from channel %s\n", s->client->fd,
             s->channel->name);
    if (s->next != NULL) {
        s->next->previous = s->previous;
    }
    if (s->previous != NULL) {
        s->previous->next = s->next;
    }
    if (s == subscription_head) {
        subscription_head = s->next;
    }

    s->channel->subscription_count--;

    if ( ! channel_has_subscription (s->channel)) {
        remove_channel (s->channel);
        destroy_channel (s->channel);
    }
}


void destroy_subscription (struct subscription *s)
{
    free (s);
}


void announce (const char *channel, const char *message)
{
    printf ("attempting to announce message %s to channel %s\n", message,
             channel);
    char *s = NULL;
    asprintf (&s, "%s!%s\n", channel, message);
    struct subscription *subscription_i = subscription_head;
    while (subscription_i != NULL) {
        printf ("testing subscription for client %d on channel %s\n",
                 subscription_i->client->fd, subscription_i->channel->name);
        if ( ! strcmp (subscription_i->channel->name, channel)) {
            printf ("announcing message %s to %d on channel %s\n", message,
                     subscription_i->client->fd, channel);
            client_write (subscription_i->client, s);
        }
        subscription_i = subscription_i->next;
    }
    free (s);
}


void subscribe (struct client *c, const char *channel_name)
{
    if (get_subscription (c, get_channel (channel_name)) != NULL) {
        printf ("client %d already subscribed to channel %s\n", c->fd, channel_name);
        return;
    }

    printf ("subscribing client %d to channel %s\n", c->fd, channel_name);

    struct subscription *subscription_i = NULL;

    if ((subscription_i = calloc (1, sizeof (struct subscription))) == NULL) {
        fanout_debug ("memory error trying to create new subscription");
        return;
    }

    subscription_i->client = c;
    subscription_i->channel = get_channel (channel_name);
    subscription_i->channel->subscription_count++;

    printf ("subscribed to channel %s\n", subscription_i->channel->name);

    subscription_i->next = subscription_head;
    if (subscription_head != NULL)
        subscription_head->previous = subscription_i;
    subscription_head = subscription_i;
}


void unsubscribe (struct client *c, const char *channel_name)
{
    struct subscription *subscription_i = subscription_head;
    if ( ! channel_exists (channel_name))
        return;
    struct channel *channel = get_channel (channel_name);

    while (subscription_i != NULL) {
        if (c == subscription_i->client && channel == subscription_i->channel) {
            remove_subscription (subscription_i);
            destroy_subscription (subscription_i);
            return;
        }
        subscription_i = subscription_i->next;
    }
}
