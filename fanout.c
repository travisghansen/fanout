/*
   A simple server in the internet domain using TCP
   MIT Licensed
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <time.h>
#include <getopt.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/resource.h>
#include <errno.h>
#include <sys/epoll.h>


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


int is_numeric (char *str);
int strcpos (const char *haystack, const char c);
char *substr (const char *s, int start, int stop);
void str_swap_free (char **target, char *source);
char *str_append (char *target, const char *data);
void fanout_error(const char *msg);
void fanout_debug (int level, const char *format, ...);
char *getsocketpeername(int fd);

int channel_exists (const char *channel_name);
int channel_has_subscription (struct channel *c);
struct channel *get_channel (const char *channel_name);
void remove_channel (struct channel *c);
void destroy_channel (struct channel *c);
u_int channel_count ();


struct client *get_client (int fd);
void remove_client (struct client *c);
void shutdown_client (struct client *c);
void destroy_client (struct client *c);
void client_write (struct client *c, const char *data);
void client_process_input_buffer (struct client *c);
u_int client_count ();


struct subscription *get_subscription (struct client *c,
                                        struct channel *channel);
void remove_subscription (struct subscription *s);
void destroy_subscription (struct subscription *s);
u_int subscription_count ();


void announce (const char *channel_name, const char *message);
void subscribe (struct client *c, const char *channel_name);
void unsubscribe (struct client *c, const char *channel_name);


// GLOBAL VARS
u_int max_client_count = 0;
u_int base_fds = 0;
u_int fd_limit = 0;
int client_limit = -1;
long server_start_time;

char ipstr[INET6_ADDRSTRLEN];

//announcement stats
unsigned long long announcements_count = 0;

//messages stats
unsigned long long messages_count = 0;

//subscription stats
unsigned long long subscriptions_count = 0;

//unsubscription stats
unsigned long long unsubscriptions_count = 0;

//ping stats
unsigned long long pings_count = 0;

//connection/client stats
unsigned long long clients_count = 0;

//over limit count
unsigned long long client_limit_count = 0;

static int daemonize =  0;

FILE *logfile;
long max_logfile_size = -1;

// 0 = ERROR
// 1 = WARNING
// 2 = INFO
// 3 = DEBUG
int debug_level = 1;
struct client *client_head = NULL;
struct subscription *subscription_head = NULL;
struct channel *channel_head = NULL;

struct rlimit s_rlimit;


int main (int argc, char *argv[])
{
    struct addrinfo *ai;
    struct addrinfo hints;
    memset (&hints, '\0', sizeof (hints));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;
    int e;
    int epollfd, nevents, efd, n, res;
    int portno = 1986;
    int optval;
    socklen_t optlen = sizeof(optval);
    u_int listen_backlog = 25;
    u_int max_events = 25;
    FILE *pidfile;
    server_start_time = (long)time (NULL);
    char buffer[1025];

    struct passwd *pwd;
    struct group *grp;

    char *user;
    char *group;
    uid_t user_id = -1;
    gid_t group_id = -1;

    struct epoll_event ev, events[max_events];

    struct client *client_i = NULL;
    struct client *client_tmp = NULL;

    socklen_t clilen;
    struct sockaddr_storage cli_addr;

    static struct option long_options[] = {
        {"port", 1, 0, 0},
        {"daemon", 0, &daemonize, 1},
        {"logfile", 1, 0, 0},
        {"pidfile", 1, 0, 0},
        {"debug-level", 1, 0, 0},
        {"help", 0, 0, 0},
        {"client-limit", 1, 0, 0},
        {"run-as", 1, 0, 0},
        {"max-logfile-size", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };

    int c;
    int option_index = 0;
    while ((c = getopt_long (argc, argv, "",
                              long_options, &option_index)) != -1) {
        switch (c) {
            case 0:
                switch (option_index) {
                    // port
                    case 0:
                        portno = atoi (optarg);
                        break;
                    // logfile
                    case 2:
                        if ((logfile = fopen (optarg, "a")) == NULL) {
                            fanout_error ("ERROR cannot open logfile");
                        }
                        break;
                    // pidfile
                    case 3:
                        if ((pidfile = fopen (optarg, "w+")) == NULL) {
                            fanout_error ("ERROR cannot open pidfile");
                        }
                        break;
                    //daemon
                    case 4:
                        debug_level = atoi (optarg);
                        break;
                    //help
                    case 5:
                        printf("Usage: fanout [options...]\n");
                        printf("pubsub style fanout server\n\n");
                        printf("Recognized options are:\n");
                        printf("  --port=PORT              port to run the serv\
ice on\n");
                        printf("                           1986 (default)\n");
                        printf("  --run-as=USER[:GROUP]    drop permissions to \
defined levels\n");
                        printf("  --daemon                 fork to background\n\
");
                        printf("  --client-limit=LIMIT     max connections\n");
                        printf("                           BEWARE ulimit \
restrictions\n");
                        printf("                           you may adjust it us\
ing ulimit -n X\n");
                        printf("                           or sysctl -w \
fs.file-max=100000\n");

                        printf("  --logfile=PATH           path to log file\n");
                        printf("  --max-logfile-size=SIZE  logfile size in MB\n\
");
                        printf("  --pidfile=PATH           path to pid file\n");
                        printf("  --debug-level=LEVEL      verbosity level\n");
                        printf("                         \
  0 = ERROR (default)\n");
                        printf("                           1 = WARNING\n");
                        printf("                           2 = INFO\n");
                        printf("                           3 = DEBUG\n");
                        printf("  --help                   show this info and e\
xit\n");
                        exit (EXIT_SUCCESS);
                        break;
                    //client-limit
                    case 6:
                        client_limit = atoi (optarg);

                        if (client_limit < 1) {
                            printf ("invalid client limit: %d\n", client_limit);
                            exit (EXIT_FAILURE);
                        }
                        break;

                    //run-as
                    case 7:
                        fanout_debug (3, "requsting permissions %s\n", optarg);
                        user = strtok (optarg, ":");
                        group = strtok (NULL, ":");
                        if (user != NULL) {
                            if (is_numeric (user)) {
                                user_id = (uid_t) atoi (user);
                            } else {
                                if ((pwd = getpwnam (user)) == NULL) {
                                    fanout_error ("failed getting user_id");
                                } else {
                                    user_id = pwd->pw_uid;
                                }
                            }
                        }

                        if (group != NULL) {
                            if (is_numeric (group)) {
                                group_id = (gid_t) atoi (group);
                            } else {
                                if ((grp = getgrnam (group)) == NULL) {
                                    fanout_error ("failed getting group_id");
                                } else {
                                    group_id = grp->gr_gid;
                                }
                            }
                        }

                        break;

                    //max-logfile-size
                    case 8:
                        fanout_debug (3, "max logfile size: %s MB\n", optarg);
                        max_logfile_size = atol (optarg);

                        break;

                }
                break;
            default:
                exit (EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        fanout_debug (0, "ERROR invalid args: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        printf ("\n");
        exit (EXIT_FAILURE);
    }

    if ( ! portno > 0) {
        fanout_debug (0, "ERROR invalid port\n");
        exit (EXIT_FAILURE);
    }

   char buf[5];
   snprintf(buf, sizeof buf, "%d", portno);

    e = getaddrinfo (NULL, buf, &hints, &ai);
    if (e != 0) {
        fanout_error ("getaddrinfo");
        exit (EXIT_FAILURE);
    }

    int nfds = 0;
    struct addrinfo *runp = ai;
    while (runp != NULL) {
        ++nfds;
        runp = runp->ai_next;
    }
    struct epoll_event fds[nfds];

    for (nfds = 0, runp = ai; runp != NULL; runp = runp->ai_next)  {
        fds[nfds].data.fd = socket (runp->ai_family, runp->ai_socktype, runp->ai_protocol);
        if (fds[nfds].data.fd == -1) {
            fanout_error ("ERROR opening socket");
            exit (EXIT_FAILURE);
        }
        fds[nfds].events = EPOLLIN;

        optval = 1;
        if (runp->ai_family==AF_INET6 && setsockopt (fds[nfds].data.fd, IPPROTO_IPV6, IPV6_V6ONLY, &optval, optlen) == -1) {
            fanout_error ("failed setting IPV6_V6ONLY");
            exit (EXIT_FAILURE);
        }

        optval = 1;
        if (setsockopt (fds[nfds].data.fd, SOL_SOCKET, SO_REUSEADDR, &optval, optlen) == -1) {
            fanout_error ("failed setting REUSEADDR");
            exit (EXIT_FAILURE);
        }

        if (bind (fds[nfds].data.fd, runp->ai_addr, runp->ai_addrlen ) != 0) {
            fanout_error ("ERROR on binding");
            exit (EXIT_FAILURE);
        } else {
            if (listen (fds[nfds].data.fd, listen_backlog) != 0) {
                fanout_error ("ERROR listening on server socket");
                exit (EXIT_FAILURE);
            }
            ++nfds;
        }
    }
    freeaddrinfo(ai);

    if((epollfd = epoll_create (nfds)) < 0)
        fanout_error ("ERROR creating epoll instance");

    for (n=0; n < nfds; n++) {
        if (epoll_ctl (epollfd, EPOLL_CTL_ADD, fds[n].data.fd, &fds[n]) == -1) {
            fanout_error ("epoll_ctl: srvsock");
            exit (EXIT_FAILURE);
        }
    }


    if (daemonize) {
        pid_t pid, sid;

        /* Fork off the parent process */
        pid = fork ();
        if (pid < 0) {
            exit (EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
            /* Write pid to file */
            if (pidfile != NULL) {
                fprintf (pidfile, "%d\n", (int) pid);
                fclose (pidfile);
            }
            exit (EXIT_SUCCESS);
        }

        /* Change the file mode mask */
        umask (0);

        /* Create a new SID for the child process */
        sid = setsid ();
        if (sid < 0) {
                /* Log any failure */
                exit (EXIT_FAILURE);
        }

        /* Close out the standard file descriptors */
        close (STDIN_FILENO);
        close (STDOUT_FILENO);
        close (STDERR_FILENO);
    }

    /* Change the current working directory */
    if ((chdir ("/")) < 0) {
        /* Log the failure */
        exit (EXIT_FAILURE);
    }


    getrlimit (RLIMIT_NOFILE,&s_rlimit);

    // epollfd, srvsock, extra for reporting busy
    base_fds = 3;

    //additional padding for safety
    base_fds += 10;

    //stdin/out/err
    if ( ! daemonize) {
        base_fds += 3;
    }

    if (logfile) {
        base_fds++;
    }

    fd_limit = s_rlimit.rlim_cur;

    if (fd_limit <= base_fds) {
        fanout_debug (0, "not enough file descriptors\n");
        exit (EXIT_FAILURE);
    }

    int calculated_client_limit = fd_limit - base_fds;

    //user defined
    if (client_limit > 0) {
        //cope with limit greater than calculated limit
        if (client_limit > calculated_client_limit) {
            struct rlimit i_rlimit;
            i_rlimit.rlim_cur = client_limit + base_fds;
            i_rlimit.rlim_max = client_limit + base_fds;
            fanout_debug (1, "attempting to set rlimit\n");
            if(setrlimit (RLIMIT_NOFILE,&i_rlimit) == -1)
                fanout_error ("ERROR setting rlimit");

            getrlimit (RLIMIT_NOFILE,&s_rlimit);
        }
    } else {
        client_limit = calculated_client_limit;
    }


    //set group first so as to not lose root permissions for user set below
    if ((int) group_id > -1) {
        fanout_debug (3, "attempting to set group %s\n",
                       getgrgid (group_id)->gr_name);
        if (setgid (group_id) == -1) {
            fanout_error ("failed setting group");
        }
    }

    //set user
    if ((int) user_id > -1) {
        fanout_debug (3, "attempting to set user %s\n", 
                       getpwuid (user_id)->pw_name);
        if (setuid (user_id) == -1) {
            fanout_error ("failed setting user");
        }
    }

    fanout_debug (1, "rlimit set at: Soft=%d Hard=%d\n", s_rlimit.rlim_cur,
                   s_rlimit.rlim_max);
    fanout_debug (2, "base fds: %d\n", base_fds);
    fanout_debug (2, "max client connections: %d\n", client_limit);

    while (1) {
        fanout_debug (3, "server waiting for new activity\n");

        if ((nevents = epoll_wait (epollfd, events, max_events, -1)) == -1)
            fanout_error ("epoll_wait");

        if (nevents == 0) {
            continue;
        }

        for (n = 0; n < nevents; n++) {
            // new connection
            efd = events[n].data.fd;

            int newconnection = 0;
            for (n=0; n < nfds; n++) {
                if (efd == fds[n].data.fd) {
                    newconnection = 1;
                    break;
                }
            }
            if (newconnection) {
                if ((client_i = calloc (1, sizeof (struct client))) == NULL) {
                    fanout_debug (0, "memory error\n");
                    continue;
                }

                clilen = sizeof (cli_addr);
                if ((client_i->fd = accept (efd,
                                             (struct sockaddr *)&cli_addr,
                                             &clilen)) == -1) {
                    fanout_debug (0, "%s\n", strerror (errno));
                    free (client_i);
                    fanout_error ("failed on accept ()");
                    continue;
                }

                int current_count = client_count ();

                if (client_limit > 0 && current_count >= client_limit) {
                    fanout_debug (1, "hit connection limit of: %d\n",
                                   client_limit);
                    send (client_i->fd, "debug!busy\n",
                           strlen ("debug!busy\n"), 0);
                    close (client_i->fd);
                    free (client_i);
                    if (client_limit_count == ULLONG_MAX) {
                        fanout_debug (1, "wow, you've limited alot..\
resetting counter\n");
                        client_limit_count = 0;
                    }
                    client_limit_count++;
                    continue;
                }

                //add new socket to watch list
                ev.events = EPOLLIN;
                ev.data.fd = client_i->fd;
                if (epoll_ctl (epollfd, EPOLL_CTL_ADD,
                     client_i->fd, &ev) == -1) {
                    fanout_error ("epoll_ctl: srvsock");
                }

                if ((setsockopt (client_i->fd, SOL_SOCKET, SO_KEEPALIVE,
                      &optval, optlen)) == -1)
                    fanout_error ("failed setting keepalive");

                //Shove current new connection in the front of the line
                client_i->next = client_head;
                if (client_head != NULL) {
                    client_head->previous = client_i;
                }
                client_head = client_i;

                current_count ++;
                if (current_count > max_client_count) {
                    max_client_count = current_count;
                }

                char *peer = getsocketpeername (client_i->fd);
                fanout_debug (2, "client socket %d connected from %s\n",
                               client_i->fd, peer);
                client_write (client_i, "debug!connected...\n");
                subscribe (client_i, "all");

                //stats
                if (clients_count == ULLONG_MAX) {
                    fanout_debug (1, "wow, you've accepted alot of connections.\
.resetting counter\n");
                    clients_count = 0;
                }
                clients_count++;

            } else {
                // Process events of other sockets...
                client_i = client_head;
                while (client_i != NULL) {
                    if (efd == client_i->fd) {
                        // Process data from socket i
                        fanout_debug (3, "processing client %d\n",
                                       client_i->fd);
                        memset (buffer, 0, sizeof (buffer));
                        res = recv (client_i->fd, buffer, 1024, 0);
                        if (res <= 0) {
                            fanout_debug (2, "client socket disconnected\n");

                            client_tmp = client_i;
                            client_i = client_i->next;

                            //del socket from watch list
                            if (epoll_ctl (epollfd, EPOLL_CTL_DEL,
                                 client_tmp->fd, &ev) == -1) {
                                fanout_error ("epoll_ctl: srvsock");
                            }
                            shutdown_client (client_tmp);
                            continue;
                        } else {
                            // Process data in buffer
                            fanout_debug (3, "%d bytes read: [%.*s]\n", res,
                                          (res - 1), buffer);
                            client_i->input_buffer = str_append (
                                                        client_i->input_buffer,
                                                        buffer);
                            client_process_input_buffer (client_i);
                        }
                    }
                    client_i = client_i->next;
                }//end while (client_i != NULL)
            }//end else
        }//end for
    }//end while (1)

    for (n=0; n < nfds; n++) {
        close (fds[n].data.fd);
    }
    return 0; 
}


int is_numeric (char *str)
{
    while (*str) {
        if (!isdigit (*str))
            return 0;
        str++;
    }
    return 1;
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
    if (data == NULL) {
        return target;
    }

    if (target == NULL) {
        asprintf (&target, "%s", data);
        return target;
    }

    int len = strlen (target) + strlen (data) + 1;
    target = realloc (target, len);

    return strcat (target, data);
}

void fanout_error(const char *msg)
{
    fanout_debug (0, "%s: %s\n", msg, strerror (errno));
    exit (1);
}


void fanout_debug (int level, const char *format, ...)
{
    char *s_level;

    switch (level) {
        case 0:
            s_level = "ERROR";
            break;
        case 1:
            s_level = "WARNING";
            break;
        case 2:
            s_level = "INFO";
            break;
        default:
            s_level = "DEBUG";
            break;
    }

    char *message;
    asprintf(&message, "[%d] %s: ", (u_int) time(NULL), s_level);
    char *data;
    va_list args;
    va_start(args, format);
    vasprintf (&data, format, args);
    va_end(args);

    message = str_append (message, data);

    if (debug_level >= level) {
        if ( ! daemonize)
            printf ("%s", message);

        if (logfile != NULL) {
            if (max_logfile_size > 0) {
                long current_pos;
                long filesize;
                if ((current_pos = ftell (logfile)) == -1)
                    exit (EXIT_FAILURE);
                //MB
                filesize = (current_pos / 1024 / 1024);
                if (filesize >= max_logfile_size) {
                    if ((ftruncate(fileno (logfile), (off_t) 0)) == -1)
                        exit (EXIT_FAILURE);
                }
            }
            fprintf (logfile, "%s", message);
            fflush (logfile);
        }
    }

    free (data);
    free (message);
}

char *getsocketpeername (int fd)
{
    struct sockaddr_storage m_addr;
    socklen_t len;
    len = sizeof m_addr;

    getpeername(fd, (struct sockaddr*)&m_addr, &len);
    getnameinfo((struct sockaddr*)&m_addr, len, ipstr, sizeof ipstr, NULL, 0, NI_NUMERICHOST);
    return ipstr;
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

    fanout_debug (2, "creating new channel %s\n", channel_name);
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
    fanout_debug (2, "removing unused channel %s\n", c->name);
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


u_int channel_count ()
{
    struct channel *channel_i = channel_head;
    u_int count = 0;

    while (channel_i != NULL) {
        count++;
        channel_i = channel_i->next;
    }
    return count;
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
    return NULL;
}


void remove_client (struct client *c)
{
    char *peer = getsocketpeername (c->fd);
    fanout_debug (3, "removing client %d connected from %s from service\n",
                   c->fd, peer);
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
    struct subscription *subscription_tmp;

    while (subscription_i != NULL) {
        subscription_tmp = subscription_i;
        subscription_i = subscription_i->next;
        if (c == subscription_tmp->client)
            unsubscribe (c, subscription_tmp->channel->name);
    }

    remove_client (c);
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

    c->output_buffer = str_append (c->output_buffer, data);

    while (strlen (c->output_buffer) > 0) {
        sent = send (c->fd, c->output_buffer, strlen (c->output_buffer), 0);
        if (sent == -1)
            break;
        fanout_debug (3, "wrote %d bytes\n", sent);
        str_swap_free (&c->output_buffer, substr (c->output_buffer, sent,
                        strlen (c->output_buffer)));
    }
    fanout_debug (3, "remaining output buffer is %d chars: %s\n",
             (u_int) strlen (c->output_buffer), c->output_buffer);
}


void client_process_input_buffer (struct client *c)
{
    char *line;
    char *message;
    char *action;
    char *channel;

    int i;

    fanout_debug (3, "full buffer\n\n%s\n\n", c->input_buffer);
    while ((i = strcpos (c->input_buffer, '\n')) >= 0) {
        line = substr (c->input_buffer, 0, i -1);
        fanout_debug (3, "buffer has a newline at char %d\n", i);
        fanout_debug (3, "line is %d chars: %s\n", (u_int) strlen (line), line);

        if ( ! strcmp (line, "ping")) {
            asprintf (&message, "%d\n", (u_int) time(NULL));
            client_write (c, message);
            free (message);
            message = NULL;
            if (pings_count == ULLONG_MAX) {
                fanout_debug (1, "wow, you've pinged alot..\
resetting counter\n");
                pings_count = 0;
            }
            pings_count++;
        } else if ( ! strcmp (line, "info")) {

            //current connections
            u_int current_client_count = client_count ();

            //channels
            u_int current_channel_count = channel_count ();

            //subscriptions
            u_int current_subscription_count = subscription_count ();
            u_int current_requested_subscriptions = (current_subscription_count
                                                      - current_client_count);
            //uptime
            long uptime = (long)time (NULL) - server_start_time;

            //averages

            asprintf (&message,
"uptime: %ldd %ldh %ldm %lds\n\
client-limit: %d\n\
limit rejected connections: %llu\n\
rlimits: Soft=%d Hard=%d\n\
max connections: %d\n\
current connections: %d\n\
current channels: %d\n\
current subscriptions: %d\n\
user-requested subscriptions: %d\n\
total connections: %llu\n\
total announcements: %llu\n\
total messages: %llu\n\
total subscribes: %llu\n\
total unsubscribes: %llu\n\
total pings: %llu\
\n",                   uptime/3600/24, uptime/3600%24,
                       uptime/60%60, uptime%60,
                       client_limit,
                       client_limit_count,
                       (int) s_rlimit.rlim_cur,
                       (int)s_rlimit.rlim_max,
                       max_client_count,
                       current_client_count, current_channel_count,
                       current_subscription_count,
                       current_requested_subscriptions, clients_count,
                       announcements_count, messages_count, subscriptions_count,
                       unsubscriptions_count, pings_count);
            client_write (c, message);
            free (message);
            message = NULL;
        } else {
            action = strtok (line, " ");
            channel = strtok (NULL, " ");
            if (action == NULL || channel == NULL) {
                fanout_debug (3, "received garbage from client\n");
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
                    fanout_debug (3, "invalid action attempted\n");
                }
            }
        }

        str_swap_free (&c->input_buffer, substr (c->input_buffer, i + 1,
                                   strlen (c->input_buffer)));
        free (line);
    }

    fanout_debug (3, "remaining input buffer is %d chars: %s\n",
             (int) strlen (c->input_buffer), c->input_buffer);
}


u_int client_count ()
{
    struct client *client_i = client_head;
    u_int count = 0;

    while (client_i != NULL) {
        count++;
        client_i = client_i->next;
    }
    return count;
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
    return NULL;
}


void remove_subscription (struct subscription *s)
{
    if (s->next != NULL) {
        s->next->previous = s->previous;
    }
    if (s->previous != NULL) {
        s->previous->next = s->next;
    }
    if (s == subscription_head) {
        subscription_head = s->next;
    }
}


void destroy_subscription (struct subscription *s)
{
    free (s);
}


u_int subscription_count ()
{
    struct subscription *subscription_i = subscription_head;
    u_int count = 0;

    while (subscription_i != NULL) {
        count++;
        subscription_i = subscription_i->next;
    }
    return count;
}


void announce (const char *channel, const char *message)
{
    fanout_debug (3, "attempting to announce message %s to channel %s\n",
                   message, channel);
    char *s = NULL;
    asprintf (&s, "%s!%s\n", channel, message);
    struct subscription *subscription_i = subscription_head;
    while (subscription_i != NULL) {
        fanout_debug (3, "testing subscription for client %d on channel %s\n",
                       subscription_i->client->fd,
                       subscription_i->channel->name);
        if ( ! strcmp (subscription_i->channel->name, channel)) {
            fanout_debug (3, "announcing message %s to %d on channel %s\n",
                           message, subscription_i->client->fd, channel);
            client_write (subscription_i->client, s);
            //message stats
            if (messages_count == ULLONG_MAX) {
                fanout_debug (1, "wow, you've sent a lot of messages..\
resetting counter\n");
                messages_count = 0;
            }
            messages_count++;
        }
        subscription_i = subscription_i->next;
    }
    fanout_debug (2, "announced messge to %d client(s) %s",
                   get_channel (channel)->subscription_count, s);
    if (announcements_count == ULLONG_MAX) {
        fanout_debug (1, "wow, you've announced alot..resetting counter\n");
        announcements_count = 0;
    }
    announcements_count++;
    free (s);
}


void subscribe (struct client *c, const char *channel_name)
{
    if (get_subscription (c, get_channel (channel_name)) != NULL) {
        fanout_debug (3, "client %d already subscribed to channel %s\n",
                       c->fd, channel_name);
        return;
    }

    struct subscription *subscription_i = NULL;

    if ((subscription_i = calloc (1, sizeof (struct subscription))) == NULL) {
        fanout_debug (1, "memory error trying to create new subscription\n");
        return;
    }

    subscription_i->client = c;
    subscription_i->channel = get_channel (channel_name);
    subscription_i->channel->subscription_count++;

    fanout_debug (2, "subscribed client %d to channel %s\n", c->fd,
                   subscription_i->channel->name);

    if (subscriptions_count == ULLONG_MAX) {
        fanout_debug (1, "wow, you've subscribed alot..resetting counter\n");
        subscriptions_count = 0;
    }
    subscriptions_count++;

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

            fanout_debug (2, "unsubscribed client %d from channel %s\n", c->fd,
                           channel_name);

            channel->subscription_count--;

            if (unsubscriptions_count == ULLONG_MAX) {
                fanout_debug (1, "wow, you've unsubscribed alot..\
resetting counter\n");
                unsubscriptions_count = 0;
            }
            unsubscriptions_count++;

            if ( ! channel_has_subscription (channel)) {
                remove_channel (channel);
                destroy_channel (channel);
            }
            return;
        }
        subscription_i = subscription_i->next;
    }
}
