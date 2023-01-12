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
#include <time.h>

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
#define MAXDATASIZE 512

/*
================ MESSAGE FORMATS ================

GAME:NEW:J1:J2 << J1 Receive new game request from player
  GAME:NEW:ID:COLOR << JX Send the game info to both players
GAME:ID:MOVE:COLOR:COORD << Jx Receive a move from player
  GAME:MOVE:COLOR:COORDS:(WAIT|PLAY) >> JX Sends the result to both players, with a list of all the impacted moves
  GAME:WON >> Jx
  GAME:LOST >> Jx
  GAME:STOP >> Jx
*/

typedef struct Game Game;
typedef struct Client Client;
typedef struct Spectator Spectator;

struct Game {
  int id;
  Client * p1;
  Client * p2;
  Spectator * spectators;
  int nb_spec;
  int grid[8][8];
  Game * next;
};

struct Client {
  int socket;
  char * alias;
  int busy;
  Spectator * spec;
  Client * next;
};

struct Spectator {
  Client * client;
  Game * game;
  Spectator * next;
};

Client * client_list = NULL;
Game * game_list = NULL;

Spectator * add_spectator(Game * game, Client * client)
{
  Spectator * spectator = (Spectator *) malloc(sizeof(Spectator));

  spectator->client = client;
  client->spec = spectator;
  spectator->game = game;
  spectator->next = game->spectators;
  game->spectators = spectator;
}

void remove_spectator(Game * game, Client * client)
{
  client->spec = NULL;
  Spectator * tmp = NULL;
  if ((game->spectators)->client == client)
  {
    tmp = game->spectators;
    game->spectators = tmp->next;
    free(tmp);
  }
  else
  {
    Spectator * current = game->spectators;
    while (current->next != NULL)
    {
      if (current->next->client == client)
      {
        tmp = current->next;
        current->next = current->next->next;
        free(tmp);
        return;
      }
    }
  }
  return;
}

// Add a client to the linked list and returns it
Client * add_client(int socket)
{
  Client * client = (Client *) malloc(sizeof(Client));
  Client * current = client_list;
  client->socket = socket;
  client->spec = NULL;
  client->busy = 0;
  client->alias = (char *) malloc(sizeof(char) * 24);
  client->next = client_list;
  client_list = client;
  return client;
}

void remove_client(int socket)
{
  Client * prev = NULL;
  Client * current = client_list;

  while (current != NULL)
  {
    if (current->socket == socket)
    {
      if (prev == NULL)
      {
        client_list = current->next;
      }
      else
      {
        prev->next = current->next;
      }
      free(current->alias);
      free(current);
      return;
    }
    prev = current;
    current = current->next;
  }
}

Client * get_client_by_socket(int socket)
{
  Client * current = client_list;

  while (current != NULL)
  {
    if (current->socket == socket)
    {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

void set_client_alias(Client * client, char * alias)
{
  sprintf(client->alias, "%s", alias);
}

Client * get_client_by_alias(char * alias)
{
  if (alias == NULL) return NULL;
  Client * current = client_list;

  while (current != NULL)
  {
    if (strcmp(current->alias, alias) == 0)
    {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

Game * get_game_from_id(int id)
{
  Game * current = game_list;
  while (current != NULL)
  {
    if (current->id == id) return current;
    current = current->next;
  }
  return NULL;
}

Game * create_game(Client * p1, Client * p2)
{
  // Seed the random number generator
  srand(time(0));

  // Generate a random number between min and max
  int min = 1;
  int max = 65536;
  int random_number;
  do {
    random_number = rand() % (max - min + 1) + min;
  } while(get_game_from_id(random_number) != NULL);
  Game * game = (Game *) malloc(sizeof(Game));
  game->id = random_number;
  p1->busy = game->id;
  p2->busy = game->id;
  game->p1 = p1;
  game->p2 = p2;
  game->spectators = NULL;
  game->nb_spec = 0;
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      game->grid[row][col] = 0;

  game->grid[3][3] = (int) (game->p2)->socket;
  game->grid[4][4] = (int) (game->p2)->socket;
  game->grid[4][3] = (int) (game->p1)->socket;
  game->grid[3][4] = (int) (game->p1)->socket;

  game->next = game_list;
  game_list = game;

  return game;
}

void remove_game(int id)
{
  Game * prev = NULL;
  Game * current = game_list;

  while (current != NULL)
  {
    if (current->id == id)
    {
      if (prev == NULL)
      {
        game_list = current->next;
      }
      else
      {
        prev->next = current->next;
      }
      (current->p1)->busy = 0;
      (current->p2)->busy = 0;
      free(current);
      return;
    }
    prev = current;
    current = current->next;
  }
}

char input[MAXDATASIZE];
char output[MAXDATASIZE];
char * token;
char * delim = ":";
int clients[MAX_CLIENTS];
char * aliases[MAX_CLIENTS];
Game games[MAX_GAMES];

void parse_name(int socket, int i);
void parse_invite(Client * from);
void parse_new_game();
void parse_spectate(Client * from);
void parse_unspectate(Client * from, int id);
void parse_game_move();

void send_list();
int send_output(int socket);
int send_output_no_clear(int socket);
int get_game_index(int id);
void send_moves_to_spec(Game * game);
void send_spec_confirm(Client * to, Client * p1, Client * p2, int id);
void print_game(Game * game);

void init_game_grid(Game * game);
int is_in_grid(int row, int col);
int valid_move(Game * game, int row, int col, int player);
void move(Game * game, int row, int col, int player, char * captured_pieces);
int can_play(Game * game, int player);
void send_result(Game * game);
void send_forfeit(Client * from);

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
  if (socket <= 0) return -1;
  int n = send(socket, output, sizeof(output), 0);
  printf("(%d) >> %ld bytes : %s\n", socket, strlen(output), output);
  memset(output, 0, sizeof(output));

  return n;
}


int send_output_no_clear(int socket)
{
  int n = send(socket, output, sizeof(output), 0);
  printf("(%d) >> %ld bytes : %s\n", socket, strlen(output), output);

  return n;
}

int main(int argc, char * argv[])
{
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

  while(1)
  {
    // Initialize the file descriptors' set for the select() call
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    // max socket is the first of the set at the beginning
    max_sd = sockfd;

    Client * current = client_list;

    // Add the clients sockets to the file descriptors' set
    while (current != NULL)
    {
      sd = current->socket;
      if (sd > 0) FD_SET(sd, &readfds);
      if (sd > max_sd) max_sd = sd;
      current = current->next;
    }

    // Monitor the sockets for readability
    activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

    // No activity
    if ((activity < 0) && (errno != EINTR))
    {
      printf("Select error\n");
      continue;
    }

    // server socket properly added to the set
    if (FD_ISSET(sockfd, &readfds))
    {
      if ((client = accept(sockfd, (struct sockaddr *) &server, (socklen_t *) &serverSize)) < 0)
      {
        printf("%s", RED);
        printf("An error occured while accepting a socket connection.\n");
        printf("%s", WHT);
        exit(EXIT_FAILURE);
      }

      Client * new_client = get_client_by_socket(client);

      if (new_client == NULL) new_client = add_client(client);

      printf("%s", YEL);
      printf("Incomming connection:\n");
      printf("%s", WHT);
      printf("Socket fd: %d\n", client);
      printf("On: ");
      printf("%s", GRN);
      printf("%s/%d\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));
      printf("%s", WHT);
    }

    current = client_list;
    while (current != NULL)
    {
      sd = current->socket;

      if (FD_ISSET(sd, &readfds))
      {
        memset(input, 0, sizeof(input));
        if ((val = read(sd, input, MAXDATASIZE)) == 0)
        {
          getpeername(sd, (struct sockaddr *) &server, (socklen_t *) &serverSize);
          printf("Outgoing connection\nUser: %s\nSocked fd: %d\nOn: %s/%d\n",
            current->alias,
            sd,
            inet_ntoa(server.sin_addr),
            ntohs(server.sin_port)
          );

          send_forfeit(current);

          int is_spec = !(current->spec == NULL);

          if (is_spec) remove_spectator(current->spec->game, current);

          FD_CLR(sd, &readfds);
          current = current->next;
          remove_client(sd);
          send_list();
        }
        else
        {
          printf("[%d bytes] From %d : %s\n", val, sd, input);

          token = strtok(input, ":");
          if (strcmp(token, "NAME") == 0)
          {
            token = strtok(NULL, "\0");
            set_client_alias(current, token);
            send_list();
          }
          else if (strcmp(token, "GAME") == 0)
          {
            token = strtok(NULL, ":");
            if (strcmp(token, "NEW") == 0) // new game request
            {
              parse_new_game();
            }
            if (strcmp(token, "INVITE") == 0) // new game request
            {
              parse_invite(current);
            }
            else if (strcmp(token, "SPECTATE") == 0)
            {
              parse_spectate(current);
            }
            else if (strcmp(token, "UNSPECTATE") == 0)
            {
              token = strtok(NULL, "\0");
              int id = (int) strtol(token, NULL, 10);
              parse_unspectate(current, id);
            }
            else if (strcmp(token, "FORFEIT") == 0)
            {
              send_forfeit(current);
              send_list();
            }
            else if (token != NULL)
            {
              int id = (int) strtol(token, NULL, 10);
              token = strtok(NULL, ":");
              if (token != NULL && strcmp(token, "MOVE") == 0)
              {
                parse_game_move(id);
              }
            }
          }
          current = current->next;
        }
      }
      else 
      {
        current = current->next;
      }
    }
  }
  free(input);
  free(output);
  close(sockfd);

  return EXIT_SUCCESS;
}

void send_spec_confirm(Client * to, Client * p1, Client * p2, int id)
{
  sprintf(output, "SPECTATE:%s:%s:%d", p1->alias, p2->alias, id);
  send_output(to->socket);
}

void send_moves_to_spec(Game * game)
{
  if (game->spectators == NULL) return;
  char bfr[256] = {0};
  char piece[5];
  for(int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      if (game->grid[i][j] == (game->p1)->socket) {
        snprintf(piece, sizeof(piece), "%d-%d:", i, j);
        strcat(bfr, piece);
      }
    }
  }

  sprintf(output, "GAME:MOVE:0:%sNULL", bfr);

  Spectator * current = game->spectators;
  while (current != NULL)
  {
    send_output_no_clear((current->client)->socket);
    current = current->next;
  }

  memset(output, 0, sizeof(output));

  memset(bfr, 0, sizeof(bfr));

  for(int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      if (game->grid[i][j] == (game->p2)->socket) {
        snprintf(piece, sizeof(piece), "%d-%d:", i, j);
        strcat(bfr, piece);
      }
    }

  sprintf(output, "GAME:MOVE:1:%sNULL", bfr);

  current = game->spectators;
  while (current != NULL)
  {
    send_output_no_clear((current->client)->socket);
    current = current->next;
  }

  memset(output, 0, sizeof(output));
}

void parse_spectate(Client * from)
{
  char * player;
  player = strtok(NULL, "\0");

  // No alias provided
  if (player == NULL || strcmp(player, from->alias) == 0) {
    return;
  }

  Client * client = get_client_by_alias(player);


  // No client found with the given aliases
  if (from == NULL || client == NULL) {
    return;
  }

  if (!client->busy) return;

  Game * game = get_game_from_id(client->busy);

  add_spectator(game, from);
  send_spec_confirm(from, game->p1, game->p2, game->id);

  send_moves_to_spec(game);
}

void parse_unspectate(Client * from, int id)
{
  if (from->spec == NULL) return;
  Game * game = get_game_from_id(id);
  remove_spectator(game, from);
  sprintf(output, "UNSPECTATE");
  send_output(from->socket);
}

void parse_invite(Client * from)
{
  // In case of a new game
  char j1[24];
  char * j2;
  
  sprintf(j1, "%s", from->alias); // Get player 1
  j2 = strtok(NULL, "\0"); // Get player 2
  // No alias provided
  if (j1 == NULL || j2 == NULL || strcmp(j1, j2) == 0) {
    return;
  }

  Client * client2 = get_client_by_alias(j2);

  // No client found with the given aliases
  if (from == NULL || client2 == NULL) {
    return;
  }

  if (from->busy || client2->busy) {
    return;
  }

  sprintf(output, "GAME:INVITE:%s", j1);
  send_output(client2->socket); // send game creation ok to players
}

void parse_new_game()
{
  // In case of a new game
  char * j1;
  char * j2;
  
  j1 = strtok(NULL, ":"); // Get player 1
  j2 = strtok(NULL, ":"); // Get player 2

  // No alias provided
  if (j1 == NULL || j2 == NULL || strcmp(j1, j2) == 0) {
    return;
  }

  Client * client1 = get_client_by_alias(j1);
  Client * client2 = get_client_by_alias(j2);

  // No client found with the given aliases
  if (client1 == NULL || client2 == NULL) {
    return;
  }

  Game * current = game_list;
  while (current != NULL)
  {
    // a player is already in a game
    if (current->p1 == client1 || current->p2 == client2) {
      return;
    }
    current = current->next;
  }

  if (client1->spec != NULL)
  {
    parse_unspectate(client1, client1->spec->game->id);
  }
  if (client2->spec != NULL)
  {
    parse_unspectate(client2, client2->spec->game->id);
  }

  current = create_game(client1, client2);

  send_list();

  sprintf(output, "GAME:NEW:%d:%d:PLAY", current->id, BLACK);
  send_output((current->p1)->socket); // send game creation ok to players
  sprintf(output, "GAME:NEW:%d:%d:WAIT", current->id, WHITE);
  send_output((current->p2)->socket); // send game creation ok to players
}

void parse_game_move(int id)
{
  char * buffer = (char *) malloc(sizeof(char) * MAXDATASIZE);

  Game * game = get_game_from_id(id);
  if (game == NULL) return;

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
    move(game, row, col, player, buffer);
    int p1_move = can_play(game, 0);
    int p2_move = can_play(game, 1);
    if (player == 0) // p1 just moved
    {
      printf("Player 1 moved\n");
      if (p2_move) // can p2 move ?
      {
        printf("Player 2 can move\n");
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p1)->socket);
        sprintf(output, "GAME:MOVE:%d:%s:PLAY", player, buffer);
        send_output((game->p2)->socket);
      }
      else if (p1_move) // can p1 move again ?
      {
        printf("Player 2 can't move but player 1 can. Change nothing to the players status\n");
        sprintf(output, "GAME:MOVE:%d:%s:PLAY", player, buffer);
        send_output((game->p1)->socket);
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p2)->socket);
      }
      else // no one can move
      {
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p1)->socket);
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p2)->socket);
        printf("Neither can move. Endgame\n");
        send_result(game);
        remove_game(game->id);
        send_list();
      }
    }
    else if (player == 1) // p2 just moved and p1 can play
    {
      printf("Player 2 just moved\n");
      if (p1_move) // can p1 move ?
      {
        printf("Player 1 can move\n");
        sprintf(output, "GAME:MOVE:%d:%s:PLAY", player, buffer);
        send_output((game->p1)->socket);
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p2)->socket);
      }
      else if (p2_move) // can p2 move again ?
      {
        printf("Player 1 can't move but player 2 can. Change nothing to the players stauts\n");
         sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
         send_output((game->p1)->socket);
         sprintf(output, "GAME:MOVE:%d:%s:PLAY", player, buffer);
         send_output((game->p2)->socket);
      }
      else // no one can move
      {
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p1)->socket);
        sprintf(output, "GAME:MOVE:%d:%s:WAIT", player, buffer);
        send_output((game->p2)->socket);
        printf("Neither can move. Endgame\n");
        send_result(game);
        remove_game(game->id);
        send_list();
      }
    }
    send_moves_to_spec(game);
    memset(buffer, 0, sizeof(buffer));
  } else {
    printf("not a valid move\n");
  }
  free(buffer);
}

void send_list()
{
  char buffer[64];

  Client * current = client_list;

  while (current != NULL)
  {
    strcpy(output, "LIST\n");
    Client * user = client_list;
    while (user != NULL)
    {
      if (user != current && strlen(user->alias) > 0)
      {
        sprintf(buffer, "%s %s\n", user->alias, user->busy ? "(busy)" : "");
        strcat(output, buffer);
      }
      user = user->next;
    }
    send_output(current->socket);
    current = current->next;
  }
}

int is_in_grid(int row, int col)
{
  return col >= 0 && col < 8 && row >= 0 && row < 8;
}

int valid_move(Game * game, int row, int col, int player)
{
  if (!is_in_grid(row, col) || game->grid[row][col]) return 0; // out of bounds or occupied

  int color = player ? (game->p2)->socket : (game->p1)->socket;
  int opponent = player ? (game->p1)->socket : (game->p2)->socket;

  int r, c;

  int directions[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
  int has_opponent_pieces = 0;
  int found_color = 0;

  for (int i = 0; i < 8; i++)
  {
    has_opponent_pieces = 0;
    found_color = 0;
    r = row + directions[i][0];
    c = col + directions[i][1];
    while (r >= 0 && r < 8 && c >= 0 && c < 8) // while in bounds
    {
      if (game->grid[r][c] == opponent) // check that the next cell is occupied by the opponent
      {
        has_opponent_pieces = 1;
      } else if (game->grid[r][c] == color && has_opponent_pieces) {
        found_color = 1;
        break;
      } else break;

      r += directions[i][0]; // update r and c
      c += directions[i][1];
    }

    if (has_opponent_pieces && found_color) {
        printf("%d - %d is a valid move for %s\n", row, col, player ? "white" : "black");
      return 1;
    }
  }

  return 0;
}

void move(Game * game, int row, int col, int player, char * captured_pieces)
{
  int color = !player ? (game->p1)->socket : (game->p2)->socket;
  int opponent = !player ? (game->p2)->socket : (game->p1)->socket;

  int r, c;

  int directions[8][2] = {{-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}};
  int has_opponent_pieces = 0;
  int found_color = 0;

  char piece[5];

  for (int i = 0; i < 8; i++)
  {
    has_opponent_pieces = 0;
    found_color = 0;
    r = row + directions[i][0];
    c = col + directions[i][1];
    while (r >= 0 && r < 8 && c >= 0 && c < 8) // while in bounds
    {
      if (game->grid[r][c] == opponent) // check that the next cell is occupied by the opponent
      {
        has_opponent_pieces = 1;
      } else if (game->grid[r][c] == color && has_opponent_pieces) {
        found_color = 1;
        break;
      } else break;

      r += directions[i][0]; // update r and c
      c += directions[i][1];
    }

    if (has_opponent_pieces && found_color)
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
  //print_game(game);
}

int can_play(Game * game, int player)
{
  int moves = 0;
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      moves += (!game->grid[i][j] && valid_move(game, i, j, player));
  return !!moves;
}

void send_result(Game * game)
{
  int p1 = 0, p2 = 0;
  Client * winner;
  Client * loser;
  int draw = -1;
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      if (game->grid[i][j] == (game->p1)->socket) p1++;
      else if (game->grid[i][j] == (game->p2)->socket) p2 ++;
    }
  }
  if (p1>p2)
  {
    winner = game->p1;
    loser = game->p2;
  }
  else if (p2>p1)
  {
    winner = game->p2;
    loser = game->p1;
  } else {
    draw = 1;
  }

  Spectator * spec = game->spectators;

  if (winner)
  {
    sprintf(output, "GAME:WON");
    send_output(winner->socket);
    sprintf(output, "GAME:SPECTATE_EOG:%s wins the game !", winner->alias);
    while (spec != NULL)
    {
      send_output_no_clear((spec->client)->socket);
      spec = spec->next;
    }
    memset(output, 0, sizeof(output));
  }
  sprintf(output, "GAME:LOST");
  if (draw == 1)
  {
    send_output((game->p1)->socket);
    send_output((game->p2)->socket);
    sprintf(output, "GAME:SPECTATE_EOG:It's a draw !");
    spec = game->spectators;
    while (spec != NULL)
    {
      send_output_no_clear((spec->client)->socket);
      spec = spec->next;
    }
    memset(output, 0, sizeof(output));
  }
  else if (draw == -1)
  {
    send_output(loser->socket);
  }
}

void send_forfeit(Client * from)
{
  Game * game = get_game_from_id(from->busy);
  if (game == NULL) return;

  sprintf(output, "%s", "GAME:WON_BY_FORFEIT");
  send_output(from->socket == (game->p1)->socket ? (game->p2)->socket : (game->p1)->socket);
  sprintf(output, "%s", "GAME:LOST_BY_FORFEIT");
  send_output(from->socket == (game->p1)->socket ? (game->p1)->socket : (game->p2)->socket);

  remove_game(game->id);
}