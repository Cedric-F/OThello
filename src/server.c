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

typedef struct Game Game;

struct Game {
  int id;
  int p1_fd;
  int p2_fd;
  char p1[10];
  char p2[10];
  int grid[8][8];
};

void send_list(int clients[], int nb, char * aliases[], char * output);
int send_message(int socket, char * message, size_t len, int flags);
void get_sock_info(int socket, char * address, char * port);
int get_sockid_from_alias(char * alias, char * aliases[], int nb);
int get_game_index(Game games[], int id, int max);
void print_game(Game * game);
void init_game_grid(Game * game);

int is_in_grid(int row, int col);
int valid_move(Game game, int row, int col, int player);
void move(Game * game, int row, int col, int player, char * captured_pieces);
int can_play(Game game, int player);
int end(Game * game);

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
  printf("(%d) >> %ld bytes : %s\n", socket, len, message);
  return n;
}

int main(int argc, char * argv[])
{
  int game_count = 0;
  int max_games = 5;
  Game games[max_games];
  char * aliases[10];
  char * token;
  char dest[24];
  int clients[10], max = 10, activity, val, sd, max_sd;
  fd_set readfds;

  char input[256];
  char output[256];
  char * buffer = (char *) malloc(sizeof(char) * 256);
  int * size;
  int i = 0;
  int sockfd;
  int client;
  struct sockaddr_in server;
  int serverSize = sizeof(server);

  memset(&server, 0, sizeof(server));

  sockfd = create_server(server);

  for (i = 0; i < max_games; i++)
  {
    games[i].id = -1;
  }

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
          send_list(clients, max, aliases, output);
        }
        else // got a message
        {
          printf("(%d) %s<< %d bytes : %s%s\n%s", sd, CYN, val, GRN, input, WHT);
          token = strtok(input, ": \n");
          if (strcmp(token, "NAME") == 0) // registered an alias name for the socket
          {
            int n;
            int match_found = 0;
            int j = -1;
            char buffer[sizeof(int) * 2];
            token = strtok(NULL, " \n\0");
            for (j = 0; j < max; j++)
            {
              if (strcmp(aliases[j], token) == 0)
              {
                memset(output, 0, sizeof output);
                strcat(output, "Username already in use. New username : ");
                strcat(output, token);
                strcat(output, "-");
                sprintf(buffer, "%d", j);
                strcat(output, buffer);
                strcat(output, "\n");
                match_found = 1;
                break;
              }
            }
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
          else if (strcmp(token, "GAME") == 0) // Game command
          {
            token = strtok(NULL, ": \n");
            printf("%s\n", token);
            if (strcmp(token, "NEW") == 0) // new game request
            { // In case of a new game
              Game game;
              char * j1;
              char * j2;
              
              j1 = strtok(NULL, ":"); // Get player 1
              if (j1 != NULL) sprintf(game.p1, "%s", j1);
              j2 = strtok(NULL, ":"); // Get player 2
              if (j2 != NULL) sprintf(game.p1, "%s", j1);

              int p_exist = 0;
              if (j1 != NULL && j2 != NULL) // If both players names are provided
              {
                int p1_fd_exists = 0;
                int p2_fd_exists = 0;
                for (int i = 0; i < max; i++) // iterate on the aliases list
                {
                  if (p1_fd_exists == 0)
                    p1_fd_exists = strcmp(j1, aliases[i]) == 0; // check if p1_fd exists
                  if (p2_fd_exists == 0)
                    p2_fd_exists = strcmp(j2, aliases[i]) == 0; // check if p2_fd exists
                  if (p_exist = (p1_fd_exists && p2_fd_exists)) break; // break if 2 matches found
                }
              }
              if (p_exist) // if players exist
              {
                if (game_count < max_games) // check the allowed game count
                {
                  int already_in_game = 0;
                  for (int i = 0; i < max_games; i++) // for each game
                  { // make sure that none of the 2 players are already playing
                    already_in_game = strcmp(j1, games[i].p1) == 0 || strcmp(j1, games[i].p2) == 0 ||
                                      strcmp(j2, games[i].p1) == 0 || strcmp(j2, games[i].p2) == 0;
                    if (already_in_game != 0) // if a game already exists then break
                    {
                      break;
                    }
                  }
                  if (already_in_game == 0) // players are not already busy
                  {
                    int game_id = -1;
                    int new = 0;
                    for (int i = 0; i < max_games; i++) // get first game that is not in play
                    {
                      if (games[i].id == -1) { // game is found
                        game_id = i;
                        new = 1;
                        break;
                      }
                    }
                    if (new)
                    {
                      int n;
                      game = games[game_id];
                      game.id = game_id; // create a new game
                      game.p1_fd = clients[get_sockid_from_alias(j1, aliases, max)];
                      game.p2_fd = clients[get_sockid_from_alias(j2, aliases, max)];
                      sprintf(game.p1, "%s", j1);
                      sprintf(game.p2, "%s", j2);
                      init_game_grid(&game);
                      game.grid[3][3] = (int) game.p2_fd;
                      game.grid[4][4] = (int) game.p2_fd;
                      game.grid[4][3] = (int) game.p1_fd;
                      game.grid[3][4] = (int) game.p1_fd;
                      memset(output, 0, sizeof output);
                      sprintf(output, "GAME:NEW:%d:0:PLAY", game_id);
                      strcat(output, "\0");
                      n = send_message(game.p1_fd, output, strlen(output), 0); // send game creation ok to players
                      if (n > 0) {
                        memset(output, 0, sizeof output);
                        sprintf(output, "GAME:NEW:%d:1:WAIT", game_id);
                        strcat(output, "\0");
                        n = send_message(game.p2_fd, output, strlen(output), 0); // send game creation ok to players
                      }
                      games[game_id] = game;
                    }
                  }
                }
              }
            }
            else if (token != NULL)
            {
              int game_id = (int) strtol(token, NULL, 10);
              game_id = get_game_index(games, game_id, max_games);
              if (game_id != -1) {
                Game game = games[game_id];
                int player;
                int col, row;
                char * tk;
                token = strtok(NULL, ":");
                if (strcmp(token, "MOVE") == 0)
                {
                  int valid;
                  int finished;
                  int n;
                  token = strtok(NULL, ":");
                  int player = (int) strtol(token, NULL, 10);
                  //player = !player ? game.p1_fd : game.p2_fd;
                  printf("player %d\n", player);
                  token = strtok(NULL, "\0");

                  tk = strtok(token, "-");
                  row = (int) strtol(tk, NULL, 10);
                  tk = strtok(NULL, "\0");
                  col = (int) strtol(tk, NULL, 10);

                  valid = valid_move(game, row, col, player);
                  if (valid)
                  {
                    printf("Valid move !\n");
                    memset(buffer, 0x00, 256);
                    move(&game, row, col, player, buffer);
                    games[game_id] = game;
                    if (!player)
                    {
                      int p1_move = !can_play(game, game.p2_fd);
                      memset(output, 0, sizeof(output));
                      sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p1_move ? "PLAY" : "WAIT"));
                      strcat(output, "\0");
                      send_message(game.p1_fd, output, sizeof(output), 0);
                      memset(output, 0, sizeof(output));
                      sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p1_move ? "WAIT" : "PLAY"));
                      strcat(output, "\0");
                      send_message(game.p2_fd, output, sizeof(output), 0);
                    }
                    else
                    {
                      int p2_move = !can_play(game, game.p1_fd);
                      memset(output, 0, sizeof(output));
                      sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p2_move ? "WAIT" : "PLAY"));
                      strcat(output, "\0");
                      send_message(game.p1_fd, output, sizeof(output), 0);
                      memset(output, 0, sizeof(output));
                      sprintf(output, "GAME:MOVE:%d:%s:%s", player, buffer, (p2_move ? "PLAY" : "WAIT"));
                      strcat(output, "\0");
                      send_message(game.p2_fd, output, sizeof(output), 0);
                    }
                    memset(output, 0, sizeof(output));
                    finished = end(&game);
                    printf("endgame : %d\n", finished);
                    if (finished)
                    {
                      int p1 = 0, p2 = 0;
                      int winner, loser, draw = -1;
                      for (int i = 0; i < 8; i++)
                      {
                        for (int j = 0; j < 8; j++)
                        {
                          if (game.grid[i][j] == game.p1_fd) p1++;
                          else if (game.grid[i][j] == game.p2_fd) p2 ++;
                        }
                      }
                      printf("score :\np1 : %dp2 : %d\n", p1, p2);
                      if (p1>p2)
                      {
                        winner = game.p1_fd;
                        loser = game.p2_fd;
                      }
                      else if (p2>p1)
                      {
                        winner = game.p2_fd;
                        loser = game.p1_fd;
                      } else {
                        draw = 1;
                      }

                      if (winner)
                      {
                        memset(output, 0, sizeof(output));
                        strcat(output, "GAME:WON");
                        strcat(output, "\0");
                        send_message(winner, output, strlen(output), 0);
                      }
                      memset(output, 0, sizeof(output));
                      strcat(output, "GAME:LOST");
                      strcat(output, "\0");
                      if (draw == 1)
                      {
                        send_message(game.p1_fd, output, strlen(output), 0);
                        send_message(game.p2_fd, output, strlen(output), 0);
                      }
                      else if (draw == -1)
                      {
                        send_message(loser, output, strlen(output), 0);
                      }
                    }
                    else if (finished == 0)
                    {
                      memset(&game, 0, sizeof(Game));
                    }
                  } else {
                    printf("not a valid move\n");
                  }
                }
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

int get_sockid_from_alias(char * alias, char * aliases[], int nb)
{
  for (int i = 0; i < nb; i++)
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

void send_list(int clients[], int nb, char * aliases[], char * output)
{
  int res;
  int n;
  for (int i = 0; i < nb; i++)
  {
    if (clients[i])
    {
      memset(output, 0, sizeof output);
      strcat(output, "LIST");
      for (int j = 0; j < nb; j++)
      {
        if (strlen(aliases[j]) > 0 && i != j)
        {
          if (j < nb) strcat(output,"\n");
          strcat(output, aliases[j]);
          if (j < nb-1) strcat(output, "\n");
        }
      }
      strcat(output, "\0");
      send_message(clients[i], output, strlen(output), 0);
    }
  }
}

int get_game_index(Game games[], int id, int max)
{
  for (int i = 0; i < max; i++)
  {
    //print_game(&(games[i]));
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
  print_game(game);
}

void print_game(Game * game)
{
  printf("Game ID : %d\n", game->id);
  printf("Player 1 : %s [%d]\n", game->p1 , game->p1_fd);
  printf("Player 2 : %s [%d]\n", game->p2 , game->p2_fd);
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
    printf("Direction %d\n", i);
    has_opponent_piece = 0;
    found_color = 0;
    r = row + directions[i][0];
    c = col + directions[i][1];
    while (r >= 0 && r < 8 && c >= 0 && c < 8) // while in bounds
    {
      printf("Moving to next coordinates\n");
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
      printf("Capturing pieces...\n");
      // Capture the opponent's pieces in this direction
      r = row + directions[i][0];
      c = col + directions[i][1];
      printf("Starting from %d-%d\n", r, c);
      while ((r != row || c != col) &&
             (r >= 0 && r < 8 && c >= 0 && c < 8) &&
             game->grid[r][c] != color)
      {
        snprintf(piece, sizeof(piece), "%d-%d", r, c);
        strcat(captured_pieces, piece);
        strcat(captured_pieces, ":");
        game->grid[r][c] = color;
        printf("Updating next coordinates\n");
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
  print_game(game);
  return (!can_play(*game, game->p1_fd) && !can_play(*game, game->p2_fd));
}


/*
Jx << GAME:NEW:J1:J2 Receive new game request
   JX >> GAME:NEW:ID:COLOR Sends the game info to both players
Jx << GAME:ID:MOVE:COLOR:COORD Receive a move
   JX >> GAME:MOVE:COLOR:COORDS:(WAIT|PLAY) Sends the result to both players, with a list of all the impacted moves
   Jx >> GAME:WIN
   Jx >> GAME:LOST
*/