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

#define BLACK 0
#define WHITE 1
#define MAX_CLIENTS 10
#define MAX_GAMES 5
#define MAXDATASIZE 256

typedef struct Game Game;

struct Game {
  int id;
  int p1_fd;
  int p2_fd;
  char p1[10];
  char p2[10];
  int * spectators;
  int grid[8][8];
};

char input[MAXDATASIZE];
char output[MAXDATASIZE];
char * token;
char * delim = ":";
int clients[MAX_CLIENTS];
char * aliases[MAX_CLIENTS];
Game games[MAX_GAMES];

void parse_name(int socket, int i);
void parse_new_game(Game * game);
void parse_game_move();

void send_list();
int send_output(int socket);
void get_sock_info(int socket, char * address, char * port);
int get_sockid_from_alias(char * alias);
int get_game_index(int id);
void print_game(Game * game);

void init_game_grid(Game * game);
int is_in_grid(int row, int col);
int valid_move(Game game, int row, int col, int player);
void move(Game * game, int row, int col, int player, char * captured_pieces);
int can_play(Game game, int player);
int end(Game * game);
void send_result(Game * game);

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

int send_output(int socket)
{
  int n = send(socket, output, strlen(output), 0);
  printf("(%d) >> %ld bytes : %s\n", socket, strlen(output), output);
  memset(output, 0, sizeof(output));

  return n;
}

int main(int argc, char * argv[])
{
  int game_count = 0;
  char dest[24];
  int activity, val, sd, max_sd;
  fd_set readfds;

  int * size;
  int i = 0;
  int sockfd;
  int client;
  struct sockaddr_in server;
  int serverSize = sizeof(server);

  memset(&server, 0, sizeof(server));

  sockfd = create_server(server);

  for (i = 0; i < MAX_GAMES; i++)
  {
    games[i].id = -1;
  }

  for (i = 0; i < MAX_CLIENTS; i++)
  {
    clients[i] = 0;
    aliases[i] = (char *) malloc(24 * sizeof(char));
  }

  while(1)
  {
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    max_sd = sockfd;

    for (i = 0; i < MAX_CLIENTS; i++)
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

      for (i = 0; i < MAX_CLIENTS; i++)
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

    for (i = 0; i < MAX_CLIENTS; i++)
    {
      sd = clients[i];

      if (FD_ISSET(sd, &readfds))
      {
        memset(input, 0, sizeof(input));
        if ((val = read(sd, input, 256)) == 0) // no read (lost connection)
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
          send_list();
        }
        else // got a message
        {
          printf("(%d) %s<< %d bytes : %s%s\n%s", sd, CYN, val, GRN, input, WHT);

          printf("before parsing\n");
          token = strtok(input, ": \n");
          printf("token %s\n", token);
          if (strcmp(token, "NAME") == 0) // registered an alias name for the socket
          {
            parse_name(sd, i);
            send_list();
          }
          else if (strcmp(token, "GAME") == 0) // Game command
          {
            Game * game = (Game *) malloc(sizeof(Game));
            token = strtok(NULL, ":");
            if (strcmp(token, "NEW") == 0) // new game request
            {
              parse_new_game(game);
            }
            else if (token != NULL)
            {
              int id = (int) strtol(token, NULL, 10);
              token = strtok(NULL, ":");
              if (strcmp(token, "MOVE") == 0)
              {
                parse_game_move(id);
              }
            }
          }
        }
      }
    }
  }
  free(input);
  free(output);
  close(sockfd);

  return EXIT_SUCCESS;
}

void parse_name(int socket, int i)
{
  int match_found = 0;
  int j = -1;
  char buffer[sizeof(int) * 2];
  token = strtok(NULL, delim);
  for (j = 0; j < MAX_CLIENTS; j++)
  {
    if (strcmp(aliases[j], token) == 0)
    {
      match_found = 1;
      break;
    }
  }
  strcpy(aliases[i], token);
  if (match_found == 1)
  {
    sprintf(buffer, "-%d", j);
    strcat(aliases[i], buffer);
    sprintf(output, "Username already in use. New username : %s", aliases[i]);
    memset(buffer, 0, sizeof buffer);
  }
  printf("New nickname set for socket %d : %s\n", i, aliases[i]);
  
  strcat(output, "Successfully registered with the username ");
  strcat(output, aliases[i]);
  send_output(socket);
}

void parse_new_game(Game * game)
{
  // In case of a new game
  char * j1;
  char * j2;
  
  j1 = strtok(NULL, ":"); // Get player 1
  j2 = strtok(NULL, ":"); // Get player 2

  int p_exist = 0;
  if (j1 != NULL && j2 != NULL) // If both players names are provided
  {
    int p1_fd_exists = 0;
    int p2_fd_exists = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) // iterate on the aliases list
    {
      if (p1_fd_exists == 0)
        p1_fd_exists = strcmp(j1, aliases[i]) == 0; // check if p1_fd exists
      if (p2_fd_exists == 0)
        p2_fd_exists = strcmp(j2, aliases[i]) == 0; // check if p2_fd exists
      if (p_exist = (p1_fd_exists && p2_fd_exists)) break; // break if 2 matches found
    }
  } else return;
  if (p_exist) // if players exist
  {
    int already_in_game = 0;
    for (int i = 0; i < MAX_GAMES; i++) // for each game
    { // make sure that none of the 2 players are already playing
      print_game(&games[i]);
      already_in_game = strcmp(j1, games[i].p1) == 0 || strcmp(j1, games[i].p2) == 0 ||
                        strcmp(j2, games[i].p1) == 0 || strcmp(j2, games[i].p2) == 0;
      if (already_in_game) // if a game already exists then break
      {
        return;
      }
    }
    int game_id = -1;
    for (int i = 0; i < MAX_GAMES; i++) // get first game that is not in play
    {
      if (games[i].id == -1) { // game is found
        game_id = i;
        break;
      }
    }
    if (game_id != -1)
    {
      sprintf(game->p1, "%s", j1);
      sprintf(game->p1, "%s", j1);
      game->id = game_id; // create a new game
      game->p1_fd = clients[get_sockid_from_alias(j1)];
      game->p2_fd = clients[get_sockid_from_alias(j2)];
      game->spectators = (int *) malloc(sizeof(int) * MAX_CLIENTS - 2);
      sprintf(game->p1, "%s", j1);
      sprintf(game->p2, "%s", j2);
      init_game_grid(game);
      
      sprintf(output, "GAME:NEW:%d:%d:PLAY", game_id, BLACK);
      strcat(output, "\0");
      send_output(game->p1_fd); // send game creation ok to players
      sprintf(output, "GAME:NEW:%d:%d:WAIT", game_id, WHITE);
      strcat(output, "\0");
      send_output(game->p2_fd); // send game creation ok to players
      games[game_id] = *game;
    } else printf("No game available\n");
  }
}

void parse_game_move(int id)
{
  char * buffer = (char *) malloc(sizeof(char) * MAXDATASIZE);
  int game_id = get_game_index(id);
  if (game_id != -1) {
    Game game = games[game_id];
    int col, row;
    char * tk;
    int valid;
    int finished;
    int n;
    token = strtok(NULL, ":");
    int player = (int) strtol(token, NULL, 10);
    token = strtok(NULL, "\0");

    tk = strtok(token, "-");
    row = (int) strtol(tk, NULL, 10);
    tk = strtok(NULL, "\0");
    col = (int) strtol(tk, NULL, 10);

    valid = valid_move(game, row, col, player);
    if (valid)
    {
      memset(buffer, 0, sizeof(buffer));
      move(&game, row, col, player, buffer);
      games[game_id] = game;
      if (!player)
      {
        int p1_move = !can_play(game, game.p2_fd);
        sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p1_move ? "PLAY" : "WAIT"));
        strcat(output, "\0");
        send_output(game.p1_fd);
        sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p1_move ? "WAIT" : "PLAY"));
        strcat(output, "\0");
        send_output(game.p2_fd);
      }
      else
      {
        int p2_move = !can_play(game, game.p1_fd);
        sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p2_move ? "WAIT" : "PLAY"));
        strcat(output, "\0");
        send_output(game.p1_fd);
        sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p2_move ? "PLAY" : "WAIT"));
        strcat(output, "\0");
        send_output(game.p2_fd);
      }

      finished = end(&game);
      printf("after end %d\n", finished);
      if (finished)
      {
        send_result(&game);
      }
      else if (finished == 0)
      {
        memset(&game, 0, sizeof(game));
      }
    } else {
      printf("not a valid move\n");
    }
    printf("After play\n");
  } else printf("No game found\n");
  free(buffer);
}

int get_sockid_from_alias(char * alias)
{
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (strcmp(alias, aliases[i]) == 0)
    {
      return i;
    }
  }
  return -1;
}

void get_sock_info(int socket, char * address, char * port)
{
  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  int res = getpeername(socket, (struct sockaddr *) &addr, &addr_size);
  sprintf(address, "%s:", inet_ntoa(addr.sin_addr));
  sprintf(port, "%d", addr.sin_port);
}

void send_list()
{
  int res;
  int n;

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i])
    {
      strcpy(output, "LIST\n");
      for (int j = 0; j < MAX_CLIENTS; j++)
      {
        if (strlen(aliases[j]) > 0 && i != j)
        {
          strcat(output, aliases[j]);
          strcat(output, "\n");
        }
      }
      strcat(output, "\0");
      send_output(clients[i]);
    }
  }
}

int get_game_index(int id)
{
  for (int i = 0; i < MAX_GAMES; i++)
  {
    if (games[i].id == id) return i;
  }
  return -1;
}

void init_game_grid(Game * game)
{
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      game->grid[i][j] = 0;
    }
  }
  game->grid[3][3] = (int) game->p2_fd;
  game->grid[4][4] = (int) game->p2_fd;
  game->grid[4][3] = (int) game->p1_fd;
  game->grid[3][4] = (int) game->p1_fd;
  print_game(game);
}

void print_game(Game * game)
{
  printf("Game ID : %d\n", game->id);
  if (game->p1 != NULL && game->p2 != NULL) {
    printf("Player 1 : %s [%d]\n", game->p1 , game->p1_fd);
    printf("Player 2 : %s [%d]\n", game->p2 , game->p2_fd);
  }
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      printf("%d ", game->grid[i][j]);
    }
    printf("\n");
  }
}

int is_in_grid(int row, int col)
{
  return col >= 0 && col < 8 && row >= 0 && row < 8;
}

int valid_move(Game game, int row, int col, int player)
{
  if (!is_in_grid(row, col) || game.grid[row][col]) return 0; // out of bounds or occupied

  int color = player ? game.p2_fd : game.p1_fd;
  int opponent = player ? game.p1_fd : game.p2_fd;

  int r, c;

  int directions[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
  int has_opponent_piece = 0;
  int found_color = 0;


  for (int i = 0; i < 8; i++)
  {
    has_opponent_piece = 0;
    found_color = 0;
    r = row + directions[i][0];
    c = col + directions[i][1];
    while (r >= 0 && r < 8 && c >= 0 && c < 8) // while in bounds
    {
      if (game.grid[r][c] == opponent) // check that the next cell is occupied by the opponent
      {
        has_opponent_piece = 1;
      } else if (game.grid[r][c] == color && has_opponent_piece) {
        found_color = 1;
        break;
      } else break;

      r += directions[i][0]; // update r and c
      c += directions[i][1];
    }

    if (has_opponent_piece && found_color) return 1;
  }

  return 0;
}

void move(Game * game, int row, int col, int player, char * captured_pieces)
{
  int color = !player ? game->p1_fd : game->p2_fd;
  int opponent = !player ? game->p2_fd : game->p1_fd;

  int r, c;

  int directions[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
  int has_opponent_piece = 0;
  int found_color = 0;

  char piece[5];

  for (int i = 0; i < 8; i++)
  {
    has_opponent_piece = 0;
    found_color = 0;
    r = row + directions[i][0];
    c = col + directions[i][1];
    while (r >= 0 && r < 8 && c >= 0 && c < 8) // while in bounds
    {
      if (game->grid[r][c] == opponent) // check that the next cell is occupied by the opponent
      {
        has_opponent_piece = 1;
      } else if (game->grid[r][c] == color && has_opponent_piece) {
        found_color = 1;
        break;
      } else break;

      r += directions[i][0]; // update r and c
      c += directions[i][1];
    }

    if (has_opponent_piece && found_color)
    {
      // Capture the opponent's pieces in this direction
      r = row + directions[i][0];
      c = col + directions[i][1];
      while ((r != row || c != col) &&
             (r >= 0 && r < 8 && c >= 0 && c < 8) &&
             game->grid[r][c] != color)
      {
        snprintf(piece, sizeof(piece), "%d-%d", r, c);
        strcat(captured_pieces, piece);
        strcat(captured_pieces, ":");
        game->grid[r][c] = color;
        r += directions[i][0]; // update r and c
        c += directions[i][1];
      }
    }
  }

  snprintf(piece, sizeof(piece), "%d-%d", row, col);
  strcat(captured_pieces, piece);
  game->grid[row][col] = color;
  print_game(game);
}


int can_play(Game game, int player)
{
  int moves = 0;
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      moves += (!game.grid[i][j] && valid_move(game, i, j, player));
  printf("Player n°%d can play %d moves\n", player, moves);
  return !!moves;
}

int end(Game * game)
{
  printf("End start\n");
  print_game(game);
  printf("End stop\n");
  return (!can_play(*game, game->p1_fd) && !can_play(*game, game->p2_fd));
}

void send_result(Game * game)
{
  int p1 = 0, p2 = 0;
  int winner, loser, draw = -1;
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      if (game->grid[i][j] == game->p1_fd) p1++;
      else if (game->grid[i][j] == game->p2_fd) p2 ++;
    }
  }
  if (p1>p2)
  {
    winner = game->p1_fd;
    loser = game->p2_fd;
  }
  else if (p2>p1)
  {
    winner = game->p2_fd;
    loser = game->p1_fd;
  } else {
    draw = 1;
  }

  if (winner)
  {
    strcat(output, "GAME:WON");
    strcat(output, "\0");
    send_output(winner);
  }
  strcat(output, "GAME:LOST");
  strcat(output, "\0");
  if (draw == 1)
  {
    send_output(game->p1_fd);
    send_output(game->p2_fd);
  }
  else if (draw == -1)
  {
    send_output(loser);
  }
}


/*
Jx << GAME:NEW:J1:J2 Receive new game request
   JX >> GAME:NEW:ID:COLOR Sends the game info to both players
Jx << GAME:ID:MOVE:COLOR:COORD Receive a move
   JX >> GAME:MOVE:COLOR:COORDS:(WAIT|PLAY) Sends the result to both players, with a list of all the impacted moves
   Jx >> GAME:WIN
   Jx >> GAME:LOST
*/