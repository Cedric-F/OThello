#include <string.h>
#include <stdio.h>
#include <errno.h> 
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"

void send_list(int clients[], int nb, char * aliases[], char * output);
int send_message(int socket, char * message, size_t len, int flags);

int create_server(struct sockaddr_in server){
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(8000);

  int sS = socket(AF_INET, SOCK_STREAM, 0);

  if (sS == -1)
  {
    perror("Socket creation failed ");
    exit(EXIT_FAILURE);
  }
  if(bind(sS, (struct sockaddr*)&server, sizeof(struct sockaddr)) < 0)
  {
    perror("Binding failed ");
    exit(EXIT_FAILURE);
  }
  if(listen(sS, 3) < 0)
  {
    perror("Connection failed ");
    exit(EXIT_FAILURE);
  }

  printf("%sServer created !\n%s", YEL, WHT);

  return sS;
}

int send_message(int socket, char * message, size_t len, int flags)
{
  int n = send(socket, message, len, flags);
  printf(">> %ld bytes : %s\n", len, message);
  return n;
}

int main(int argc, char * argv[])
{
  char * aliases[10];
  char * token;
  char dest[24];
  int clients[10], max = 10, activity, val, sd, max_sd;
  fd_set readfds;

  char input[257];
  char output[257];
  int * size;
  int i = 0;
  int sockfd;
  int client;
  struct sockaddr_in server;
  int serverSize = sizeof(server);

  memset(&server, 0, sizeof(server));

  sockfd = create_server(server);

  for (i = 0; i < max; i++)
  {
    clients[i] = 0;
    aliases[i] = (char *) malloc(24 * sizeof(char));
  }

  while(1)
  {
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    max_sd = sockfd;

    for (i = 0; i < max; i++)
    {
      sd = clients[i];
      if (sd > 0) FD_SET(sd, &readfds);
      if (sd > max_sd) max_sd = sd;
    }

    activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) printf("Select error\n");

    if (FD_ISSET(sockfd, &readfds))
    {
      if ((client = accept(sockfd, (struct sockaddr *) &server, (socklen_t *) &serverSize)) < 0)
      {
        printf("%s", RED);
        printf("An error occured while accepting a socket connection.\n");
        printf("%s", WHT);
        exit(EXIT_FAILURE);
      }

      for (i = 0; i < max; i++)
      {
        if (clients[i] == 0)
        {
          clients[i] = client;
          break;
        }
      }

      printf("%s", YEL);
      printf("Incomming connection:\n");
      printf("%s", WHT);
      printf("Socket fd: %d\n", client);
      printf("On: ");
      printf("%s", GRN);
      printf("%s/%d\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));
      printf("%s", WHT);
    }

    for (i = 0; i < max; i++)
    {
      sd = clients[i];

      if (FD_ISSET(sd, &readfds))
      {
        if ((val = read(sd, input, 256)) == 0)
        {
          getpeername(sd, (struct sockaddr *) &server, (socklen_t *) &serverSize);
          printf("%sOutgoing connection:\n%s", YEL, WHT);
          printf("User: %s\n", aliases[i]);
          printf("Socket fd: %d\nOn: %s/%d\n"
            , sd
            , inet_ntoa(server.sin_addr)
            , ntohs(server.sin_port));
          close(sd);
          clients[i] = 0;
          memset(aliases[i], 0, 24);
          send_list(clients, max, aliases, output);
        } else
        {
          printf("%s<< %d bytes : %s%s\n%s", CYN, val, GRN, input, WHT);


          token = strtok(input, " \n");
          //printf("%d\n", strcmp(token, "SEND"));
          if (strcmp(token, "SEND") == 0)
          {
            if (!strlen(aliases[i]))
            {
              memset(output, 0, sizeof output);
              strcat(output, "Please sign in using the NAME command.\nType HELP for more info.\n");
              send_message(sd, output, strlen(output), 0);
            }
            else
            {
              token = strtok(NULL, " \n");
              printf("%s\n", token);
              if (token != NULL)
              {
                strcpy(dest, token);
                token = strtok(NULL, "\0");
                if (token != NULL)
                {
                  printf("%s %s\n", dest, token);
                  int n = 0;
                  for (int j = 0; j < max; j++)
                  {
                    if (strcmp(dest, aliases[j]) == 0)
                    {
                      memset(output, 0, sizeof output);
                      strcat(output, aliases[i]);
                      strcat(output, " sent you:\n");
                      strcat(output, token);
                      n = send_message(clients[j], output, strlen(output), 0);
                      break;
                    }
                  }
                  if (n == 0)
                  {
                    memset(output, 0, sizeof output);
                    strcat(output, "User not found.\n");
                    send_message(sd, output, strlen(output), 0);
                  }
                }
              }
            }
          }
          else if (strcmp(token, "NAME") == 0)
          {
            //printf("name registration\n");
            int n;
            int match_found = 0;
            int j = -1;
            char buffer[sizeof(int) * 2];
            token = strtok(NULL, " \n\0");
            //printf("entering verification loop\n");
            for (j = 0; j < max; j++)
            {
              //printf("%s is being checked against %s\n", aliases[j], token);
              if (strcmp(aliases[j], token) == 0)
              {
                //printf("Match already in use !\n");
                memset(output, 0, sizeof output);
                strcat(output, "Username already in use. New username : ");
                strcat(output, token);
                strcat(output, "-");
                sprintf(buffer, "%d", j);
                strcat(output, buffer);
                strcat(output, "\n");
                //n = send_message(sd, output, strlen(output), 0);
                match_found = 1;
                break;
              }
              //printf("Not a match, keep looking\n");
            }
            //printf("Registered okay\n");
            strcpy(aliases[i], token);
            if (match_found == 1)
            {
              strcat(aliases[i], "-");
              strcat(aliases[i], buffer);
              memset(buffer, 0, sizeof buffer);
            }
            printf("New nickname set for socket %d : %s\n", i, aliases[i]);
            memset(output, 0, sizeof output);
            strcat(output, "Successfully registered with the username ");
            strcat(output, aliases[i]);
            strcat(output, "\n\0");
            //n = send_message(sd, output, strlen(output), 0);
            send_list(clients, max, aliases, output);
          }

          memset(input, 0, sizeof input);
        }
      }
    }
  }
  free(input);
  free(output);
  close(sockfd);

  return EXIT_SUCCESS;
}

void send_list(int clients[], int nb, char * aliases[], char * output)
{
  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  char address[20];
  char port[10];
  int res;
  int n;
  for (int i = 0; i < nb; i++)
  {
    if (clients[i])
    {
      memset(output, 0, sizeof output);
      strcat(output, "Connected users:\n");
      for (int j = 0; j < nb; j++)
      {
        if (strlen(aliases[j]) > 0 && i != j)
        {
          memset(address, 0, sizeof(address));
          res = getpeername(clients[j], (struct sockaddr *) &addr, &addr_size);
          sprintf(address, "%s", inet_ntoa(addr.sin_addr));
          sprintf(port, "%d", addr.sin_port);
          strcat(output, aliases[j]);
          strcat(output, "[");
          strcat(output, address);
          strcat(output, ":");
          strcat(output, port);
          strcat(output, "]");
          strcat(output, "\n");
        }
      }
      strcat(output, "\0");
      send_message(clients[i], output, strlen(output), 0);
    }
  }
}