// first for FreeBSD's broken <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define true 1
#define false 0

void append_thread_death(int which)
{
  FILE *tmp;

  if (NULL != (tmp = fopen("/thread_death_log", "a")))
  {
    fprintf(tmp, "Thread #%d died!\n", which);
    fclose(tmp);
  }
}

volatile int shutdown_proxy = false;

void *inbound_proxy_loop(void *arg)
{
  char buffer[1024];
  int *sock = (int *)arg;

  while (true)
  {
    int bytes_read = recv(sock[1], buffer, 1024, 0);

    if (bytes_read <= 0)
      break;

    if (shutdown_proxy)
      break;

    send(sock[0], buffer, bytes_read, 0);
  }

  shutdown_proxy = true;
  //exit(0); // I didn't want to do this, but... =P
  //append_thread_death(2);
  return NULL;
}

int *g_sock;

void kill_sockets()
{
  if (g_sock[0])
    close(g_sock[0]);
  if (g_sock[1])
    close(g_sock[1]);
}

int proxy_loop(int sockfd, struct sockaddr_in target_sockaddr)
{
  pthread_t thread_id;
  int sock[2];

  g_sock = sock;
  sock[0] = sock[1] = 0;

  atexit(kill_sockets);

  sock[0] = sockfd;

  sock[1] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (connect(sock[1], (struct sockaddr *)&target_sockaddr, sizeof(target_sockaddr)))
    return 1;

  pthread_create(&thread_id, NULL, inbound_proxy_loop, sock);

  {
    char buffer[1024]; // allocate on stack after call to pthread_create
    while (true)
    {
      int bytes_read = recv(sock[0], buffer, 1024, 0);

      if (bytes_read <= 0)
        break;

      if (shutdown_proxy)
        break;

      send(sock[1], buffer, bytes_read, 0);
    }

    shutdown_proxy = true;
  }
  //exit(0); // I didn't want to do this, but... =P
  //append_thread_death(1);
  return 0; // returns from 'main'
}

char *app;

void error_abort(char *message)
{
  fprintf(stderr, "%s\n\nusage: %s listenport targetip targetport allowedsource [allowedsource [..]]\n\n", message, app);
  exit(1);
}

int main(int argc, char *argv[])
{
  int port_number, sockfd;

  app = argv[0];

  if (argc < 2)
    error_abort("need to know what port number to read from");

  if (argc < 3)
    error_abort("need to know what ip to forward to");

  if (argc < 4)
    error_abort("need to know what port number to forward to");

  if (argc < 5)
    fprintf(stderr, "no sources specified! all connections will be allowed\n");

  port_number = atoi(argv[1]);

  unsigned long target_address = inet_addr(argv[2]);
  int target_port = atoi(argv[3]);

  struct sockaddr_in target_sockaddr;

  memset(&target_sockaddr, 0, sizeof(target_sockaddr));
  target_sockaddr.sin_family = AF_INET;
  memcpy(&target_sockaddr.sin_addr, &target_address, 4);
  target_sockaddr.sin_port = htons(target_port);

  printf("Port number: %d\n", port_number);
  printf("Tunnel to: %s:%d\n", inet_ntoa(target_sockaddr.sin_addr), target_port);

  sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 
  if (sockfd <= 0)
    error_abort("unable to create socket!");
  else
  {
    struct sockaddr_in zero_address;
    int enabled=1;

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    memset(&zero_address, 0, sizeof(zero_address));
    zero_address.sin_family = AF_INET;
    zero_address.sin_port = htons(port_number);

    if (bind(sockfd, (struct sockaddr *)&zero_address, sizeof(zero_address)))
      error_abort("unable to bind socket!");
    else if (listen(sockfd, 20))
      error_abort("unable to listen on socket!");
    else
    {
      int num_sources = argc - 4;
      unsigned long *source = malloc(num_sources * sizeof(unsigned long));

      int i;

      for (i=0; i<num_sources; i++)
        source[i] = inet_addr(argv[i + 4]);

      { // stop the zombie processes
        struct sigaction sa, osa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sa.sa_sigaction = NULL;
        sa.sa_flags = SA_NOCLDWAIT;

        if (sigaction(SIGCHLD, &sa, &osa))
          fprintf(stderr, "note: zombie processes will occur; SIGCHLD handler could not be changed\n");
      }

      while (true)
      {
        struct sockaddr_in sockname;
        socklen_t sockname_size = sizeof(sockname);
        int child_sockfd = accept(sockfd, (struct sockaddr *)&sockname, &sockname_size);

        int wait_status;
        while (wait3(&wait_status, WNOHANG, NULL) > 0)
          ;

        if (child_sockfd <= 0)
          continue;

        for (i=0; i<num_sources; i++)
          if (0 == memcmp(&sockname.sin_addr, &source[i], 4))
            break;

        if (num_sources && (i >= num_sources))
        {
          close(child_sockfd);
          continue;
        }

        i = fork();
        if (i == -1)
          close(child_sockfd);
        if (i == 0)
          return proxy_loop(child_sockfd, target_sockaddr);
        //printf("new pid: %d\n", i);
      }
    }
  }
}
