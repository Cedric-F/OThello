#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <inttypes.h>
#include <arpa/inet.h>

#include <signal.h>
#include <sys/signalfd.h>

#include <gtk/gtk.h>
#include <glib.h>
#include <X11/Xlib.h>

/*
============ THREAD SAFE INTERFACE UPDATE FUNCTIONS ============
*/

/*
Each of these functions update the interface in a thread safe manner
That is, they are passed as callbacks into GTK main context's thread
to be added in its event loop. The data is passed through a gpointer
and the functions return FALSE (in this case) to avoid being called
again in the even loop.
*/
gboolean disable_game_controls(gpointer data);
gboolean enable_game_controls(gpointer data);
gboolean affich_joueur_buffer(gpointer data);
gboolean reset_liste_joueurs(gpointer data);
gboolean affiche_fenetre_fin(gpointer data);
gboolean update_white_label(gpointer data);
gboolean update_black_label(gpointer data);
gboolean update_white_score(gpointer data);
gboolean update_black_score(gpointer data);
gboolean update_spec_button(gpointer data);
gboolean reset_interface(gpointer data);
gboolean prompt_invite(gpointer data);
gboolean update_title(gpointer data);
gboolean count_score(gpointer data);
gboolean update_move(gpointer data);
gboolean init_game(gpointer data);

/*
============ STATIC USER TRIGGERED CALLBACK FUNCTIONS ============
*/

static void player_move(GtkWidget *p_case);
static void server_connect(GtkWidget *b);
static void forfeit_game(GtkWidget *b);
static void start_game(GtkWidget *b);
static void clear_game(GtkWidget *b);
static void spectate(GtkWidget *b);

/*
============ UTILS ============
*/

#define MAXDATASIZE 512

void coord_to_indexes(const gchar *coord, int *col, int *row);
void indexes_to_coord(int col, int row, char * coord);
void change_img_case(int col, int row, int color);
void disable_server_connect(void);
void signup(char * login);

char *get_server_address(void);
char *get_server_port(void);
char *get_target(void);
char *get_login(void);

/*
============ STRUCTURES ============
*/

typedef struct State State;
typedef struct Move Move;
typedef struct G_Data G_Data;
typedef struct PromptData PromptData;

struct State {
  int * sockfd;
  int * play;
};

struct Move {
  int col;
  int row;
  int player;
};

struct G_Data {
  GtkBuilder * p_builder;
  char data[24];
};

struct PromptData {
  char from[32];
  char to[32];
  int socket_fd;
};

/*
============ GLOBALS ============
*/

struct sockaddr_in server;
int serverSize = sizeof(server);
int sockfd;
int damier[8][8];
int couleur;
int game_id = -1;
char * login;
char * target_name;
State * state;
pthread_t read_thread;

// GTK variables
GtkBuilder  *  p_builder   = NULL;
GError      *  p_err       = NULL;

// Thread de lecture

gboolean heart();

void * t_read(void * state)
{
  // We'll need the main loop context for thread-safe updates of our interface
  GMainContext *main_context = g_main_context_default();

  State * st = (State *) state;
  char buffer[MAXDATASIZE]; // server input
  ssize_t size; // input size

  char * token; // will hold the commands tokens

  // window titles
  char * wait = g_strdup("Wait for your turn");
  char * play = g_strdup("Your turn to play");
  char * won_message = g_strdup("Fin de la partie.\n\nVous avez gagné!");
  char * lost_message = g_strdup("Fin de la partie.\n\nVous avez perdu!");
  char * forfeit_won_message = g_strdup("Fin de la partie.\n\nVictoire par abandon!");
  char * forfeit_lost_message = g_strdup("Fin de la partie.\n\nDéfaite par abandon!");
  
  g_main_context_invoke(main_context, (GSourceFunc) heart, NULL);

  while(1)
  {
    // Clearing the buffer for next input
    memset(buffer, 0, MAXDATASIZE);

    // continuously reading from the server
    size = read(*(st->sockfd), buffer, MAXDATASIZE);
    if (size == 0)
    {
      printf("\nLost connection to server...\n");
      exit(EXIT_FAILURE);
    }
    else if (size >= 1) // Message received
    {
      fflush(stdout);
      printf("\n<< [%ld bytes] %s\n", size, buffer);

      // Expected values :
      // LIST, GAME:<*>
      token = strtok(buffer, "\n");

      if (strcmp(token, "LIST") == 0)
      {
        g_main_context_invoke(main_context, (GSourceFunc) reset_liste_joueurs, p_builder);
        do
        {
          token = strtok(NULL, "\n");
          if (token != NULL)
          {
            G_Data * data = (G_Data *) malloc(sizeof(G_Data));
            data->p_builder = p_builder;
            sprintf(data->data, "%s", token);
            g_main_context_invoke(main_context, (GSourceFunc) affich_joueur_buffer, data);
          }
        } while (token != NULL);
      }
      else // Not a new user list
      {
        token = strtok(buffer, ":");
        if (token != NULL)
        {
          // Making sure that we received a GAME input
          if (strcmp(token, "GAME") == 0)
          {
            // Sub command of the game input
            // Expected values : NEW:ID:Player:Player, MOVE:Player:Coords:Status
            token = strtok(NULL, ":");

            // Command is a new game request
            if (token != NULL && strcmp(token, "NEW") == 0)
            {
              // Safely disable the game_start button in the main thread
              g_main_context_invoke(main_context, (GSourceFunc) disable_game_controls, p_builder);

              // Game ID to send back to the server
              token = strtok(NULL, ":");
              if (token != NULL)
              {
                game_id = strtol(token, NULL, 10);

                // Player's position (0 for black, 1 for white)
                token = strtok(NULL, ":");
                couleur = (int) strtol(token, NULL, 10);

                // Safely initialize the game interface in the main thread
                g_main_context_invoke(main_context, (GSourceFunc) init_game, damier);

                // Game status for the player
                // Expected values : WAIT or PLAY
                token = strtok(NULL, "\0");
                if (token != NULL && strcmp(token, "WAIT") == 0)
                {
                  // Safely update the window's title with the player's turn in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) update_title, wait);
                  *(st->play) = 0;
                }
                else if (token != NULL && strcmp(token, "PLAY") == 0)
                {
                  // Safely update the window's title with the player's turn in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) update_title, play);
                  *(st->play) = 1;
                }
              }
            } else if (strcmp(token, "INVITE") == 0)
            {
              token = strtok(NULL, "\0");
              PromptData prompt;
              sprintf(prompt.from, "%s", token);
              sprintf(prompt.to, "%s", login);
              prompt.socket_fd = sockfd;
              g_main_context_invoke(main_context, (GSourceFunc) prompt_invite, &prompt);
            }
            // Command is a move update
            // Expected values : MOVE:Player:<coord-list>:Status
            else if (token != NULL && strcmp(token, "MOVE") == 0)
            {
              // Move's color (0 for black and 1 for white)
              int move = (int) strtol(strtok(NULL, ":"), NULL, 10);
              // We iterate over the tokens until the end of the string
              while((token = strtok(NULL, ":\0")) != NULL)
              {
                // If the token is not a game status (WAIT / PLAY)
                if (strcmp(token, "PLAY") && strcmp(token, "WAIT") && strcmp(token, "NULL"))
                {
                  // token is a set of coordinates <row>-<col>

                  char *ptr = token;
                  // get the row position
                  char * r = strsep(&ptr, "-");
                  // get the column position
                  char * c = strsep(&ptr, "\0");

                  int col, row;
                  // convert r and c into integers
                  row = (int) strtol(r, NULL, 10);
                  col = (int) strtol(c, NULL, 10);

                  // update the grid's data with the move's color at the given position
                  damier[row][col] = move;

                  // Stores the move's data
                  Move * m = (Move *) malloc(sizeof(Move));
                  m->col = col;
                  m->row = row;
                  m->player = move;
                  // Safely update the game's interface with the move's date in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) update_move, m);

                  // Safely update the player's score in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) count_score, damier);
                }
                // We reach the end of the coordinate's list with a game status
                else if (strcmp(token, "WAIT") == 0)
                {
                  // Safely update the window's title in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) update_title, wait);
                  *(st->play) = 0;
                }
                else if (strcmp(token, "PLAY") == 0)
                {
                  // Safely update the window's title in the main thread
                  g_main_context_invoke(main_context, (GSourceFunc) update_title, play);
                  *(st->play) = 1;
                }
              }
            }
            // Game ends with the winning message
            else if (token != NULL && strcmp(token, "WON") == 0)
            {
              game_id = -1;
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez gagné !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, won_message);
              g_main_context_invoke(main_context, (GSourceFunc) enable_game_controls, p_builder);
            }
            // Game ends with the losing message
            else if (token != NULL && strcmp(token, "LOST") == 0)
            {
              game_id = -1;
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez perdu !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, lost_message);
              g_main_context_invoke(main_context, (GSourceFunc) enable_game_controls, p_builder);
            }
            else if (token != NULL && strcmp(token, "WON_BY_FORFEIT") == 0)
            {
              game_id = -1;
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez perdu !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, forfeit_won_message);
              g_main_context_invoke(main_context, (GSourceFunc) enable_game_controls, p_builder);
            }
            else if (token != NULL && strcmp(token, "LOST_BY_FORFEIT") == 0)
            {
              game_id = -1;
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez perdu !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, forfeit_lost_message);
              g_main_context_invoke(main_context, (GSourceFunc) enable_game_controls, p_builder);
            }
            else if (token != NULL && strcmp(token, "SPECTATE_EOG") == 0)
            {
              char result[64];
              token = strtok(NULL, "\0");
              sprintf(result, "Fin de partie.\n\n%s !", token);
              char * message = g_strdup(result);
              game_id = -1;
              g_main_context_invoke(main_context, (GSourceFunc) enable_game_controls, p_builder);
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, message);
            }
          }
          else if (strcmp(token, "UNSPECTATE") == 0)
          {
            g_main_context_invoke(main_context, (GSourceFunc) reset_interface, p_builder);
          }
          else if (strcmp(token, "SPECTATE") == 0)
          {
            char * p1;
            char * p2;
            char title[64];

            for (int i = 0; i < 8; i++) {
              for (int j = 0; j < 8; j++) {
                damier[i][j] = -1;
                Move * move = (Move *) malloc(sizeof(Move));
                move->col = j;
                move->row = i;
                move->player = -1;
                g_main_context_invoke(main_context, (GSourceFunc) update_move, move);
              }
            }

            token = strtok(NULL, ":");
            p1 = g_strdup(token);
            
            token = strtok(NULL, ":");
            p2 = g_strdup(token);

            token = strtok(NULL, "\0");
            game_id = (int) strtol(token, NULL, 10);
            *(st->play) = 0;

            sprintf(title, "Spectating %s vs %s", p1, p2);

            G_Data * data = (G_Data *) malloc(sizeof(G_Data));
            data->p_builder = p_builder;
            sprintf(data->data, "Unspectate");

            g_main_context_invoke(main_context, (GSourceFunc) update_title, title);
            g_main_context_invoke(main_context, (GSourceFunc) update_black_label, p1);
            g_main_context_invoke(main_context, (GSourceFunc) update_white_label, p2);
            g_main_context_invoke(main_context, (GSourceFunc) update_spec_button, data);
          }
        }
      }
    }
    else if (size < 0)
    {
      perror("An error occured while trying to read from the server\n");
      exit(EXIT_FAILURE);
    }
  }
  exit(EXIT_SUCCESS);
}

gboolean prompt_invite(gpointer data)
{
  PromptData *promptData = (PromptData *) data;
  char title[64];
  sprintf(title, "%s wants to play with you!", promptData->from);

  // Create a dialog with yes/no buttons
  GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_YES_NO,
      title);

  // Show the dialog and wait for a response
  int response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  char message[128];
  // Send the response to the server
  if (response == GTK_RESPONSE_YES) {
    sprintf(message, "GAME:NEW:%s:%s", promptData->from, promptData->to);
    send(promptData->socket_fd, message, sizeof(message), 0);
  } else {
    sprintf(message, "DECLINE");
    send(promptData->socket_fd, message, sizeof(message), 0);
  }
  memset(message, 0, sizeof(message));
  return FALSE;
}

gboolean update_title(gpointer data)
{
  GtkWidget * p_win = (GtkWidget *) gtk_builder_get_object (p_builder, "window1");
  char * text = (char *) data;
  gtk_window_set_title((GtkWindow *) p_win, text);
  return FALSE;
}

gboolean update_move(gpointer data)
{
  Move * m = (Move *) data;
  int col, row, player;
  col = m->col;
  row = m->row;
  player = m->player;

  char * coord;

  coord=malloc(3*sizeof(char));

  indexes_to_coord(col, row, coord);

  if(player == 1)
  { // image pion blanc
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_blanc.png");
  }
  else if(player == 0)
  { // image pion noir
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_noir.png");
  }
  else if(player == -1)
  {
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_def.png");
  }
  free(m);
  return FALSE;
}

gboolean update_white_label(gpointer data)
{
  char * text = (char *) data;
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J1")), text);
  return FALSE;
}

gboolean update_black_label(gpointer data)
{
  char * text = (char *) data;
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J2")), text);
  return FALSE;
}


gboolean update_white_score(gpointer data)
{
  int * score = (int *) data;
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d", *score);
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ1")), s);
  return FALSE;
}

gboolean update_black_score(gpointer data)
{
  int * score = (int *) data;
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d", *score);
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ2")), s);
  return FALSE;
}

/* Fonction transforme coordonnees du damier graphique en indexes pour matrice du damier */
void coord_to_indexes(const gchar *coord, int *col, int *row)
{
  char *c;
  
  c=malloc(3*sizeof(char));
  
  c=strncpy(c, coord, 1);
  c[1]='\0';

  *col = ((int) c[0] - 65);
  
  *row=atoi(coord+1)-1;
}

/* Fonction transforme coordonnees du damier graphique en indexes pour matrice du damier */
void indexes_to_coord(int col, int row, char *coord)
{
  char c;
  c=(char) ('A' + col % 26);
  sprintf(coord, "%c%d", c, row+1);
}

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int row, int color)
{
  char * coord;
  coord=malloc(3*sizeof(char));
  indexes_to_coord(col, row, coord);
  if(color == 1)
  { // image pion blanc
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_blanc.png");
  }
  else if(color == 0)
  { // image pion noir
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_noir.png");
  }
  else if (color == 2)
  {
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_rouge.png");
  }
  else gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_def.png");
}

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *get_server_address(void)
{
  GtkWidget *entry_addr_srv;
  
  entry_addr_srv = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_adr");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_addr_srv));
}

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *get_server_port(void)
{
  GtkWidget *entry_port_srv;
  
  entry_port_srv = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_port");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_port_srv));
}

/* Fonction retournant texte du champs login de l'interface graphique */
char *get_login(void)
{
  GtkWidget *entry_login;
  
  entry_login = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_login");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_login));
}

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *get_target(void)
{
  GtkWidget * entry_target_name;
  
  entry_target_name = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_target_name");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_target_name));
}

/* Fonction affichant boite de dialogue si partie gagnee */
gboolean affiche_fenetre_fin(gpointer data)
{
  char * message = (char *) data;
  GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;
  GtkWidget * dialog = gtk_message_dialog_new(NULL, flags, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", message);
  gtk_dialog_run(GTK_DIALOG (dialog));
  
  gtk_widget_destroy(dialog);
  return FALSE;
}

gboolean update_spec_button(gpointer data)
{
  G_Data * gdata = (G_Data *) data;
  GtkBuilder * p_builder = (GtkBuilder *) gdata->p_builder;
  GObject * button = gtk_builder_get_object(p_builder, "spectate");
  if (button != NULL && GTK_IS_BUTTON(button)) {
    char * unspectate = g_strdup(gdata->data);
    gtk_button_set_label(GTK_BUTTON(button), unspectate);
  }
  free(gdata);
  return FALSE;
}

gboolean reset_interface(gpointer data)
{
  GtkBuilder * p_builder = (GtkBuilder *) data;
  char * spectate = g_strdup("Spectate");
  char * white = g_strdup("Player 1");
  char * black = g_strdup("Player 2");
  char * title = g_strdup("Projet Othello");
  update_black_label(black);
  update_white_label(white);
  update_title(title);
  gtk_button_set_label((GtkButton *) gtk_builder_get_object(p_builder, "spectate"), spectate);
  free(spectate);
  return FALSE;
}

void signup(char * login)
{
  int n;
  char message[5 + strlen(login)];
  sprintf(message, "NAME:%s", login);
  n = send(sockfd, message, strlen(message), 0);
  printf("\n>> [%d bytes] : %s\n", n, message);
}

void disable_server_connect(void)
{
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_connect"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), TRUE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_login"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_port"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_adr"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "spectate"), TRUE);
}

gboolean disable_game_controls(gpointer data)
{
  GtkBuilder * p_builder = (GtkBuilder *) data;
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "spectate"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "forfeit"), TRUE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "clear"), FALSE);
  return FALSE;
}

gboolean enable_game_controls(gpointer data)
{
  GtkBuilder * p_builder = (GtkBuilder *) data;
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), TRUE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "spectate"), TRUE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "forfeit"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "clear"), TRUE);
  return FALSE;
}

/* Fonction permettant d'initialiser le plateau de jeu */
gboolean init_game(gpointer data)
{
  int (*damier)[8] = (int(*)[8])data;
  char * text_you = g_strdup("You");
  char * text_opponent = g_strdup("Opponent");
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      damier[i][j] = -1;
      change_img_case(i, j, -1);
    }
  // Initilisation du damier (D4=blanc, E4=noir, D5=noir, E5=blanc)
  change_img_case(3, 3, 1);
  change_img_case(4, 3, 0);
  change_img_case(3, 4, 0);
  change_img_case(4, 4, 1);

  damier[3][3] = 1;
  damier[4][3] = 0;
  damier[3][4] = 0;
  damier[4][4] = 1;
  
  // Initialisation des scores et des joueurs
  if(couleur == 1)
  {
    update_white_label(text_you);
    update_black_label(text_opponent);
  }
  else if(couleur == 0)
  {
    update_black_label(text_you);
    update_white_label(text_opponent);
  }
  int white = 2;
  int black = 2;
  update_white_score(&white);
  update_black_score(&black);

  return FALSE;
}

/* Fonction reinitialisant la liste des joueurs sur l'interface graphique */
gboolean reset_liste_joueurs(gpointer data)
{
  GtkBuilder * p_builder = (GtkBuilder *) data;
  GtkTextIter start, end;
  
  gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start);
  gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &end);
  
  gtk_text_buffer_delete(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start, &end);
  return FALSE;
}

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
gboolean affich_joueur_buffer(gpointer data)
{
  G_Data * arg = (G_Data *) data;
  char * login = arg->data;
  GtkBuilder * p_builder = arg->p_builder;
  const gchar *joueur;
  
  joueur=g_strconcat(login, "\n", NULL);
  gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), joueur, strlen(joueur));
  free(data);
  return FALSE;
}

gboolean count_score(gpointer data)
{
  int (*damier)[8] = (int(*)[8])data;
  int nb_p1 = 0;
  int nb_p2 = 0;
  //printf("Counting scores\n");
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      //printf("%d ", damier[i][j]);
      if (damier[i][j] == 1) nb_p1++;
      else if (damier[i][j] == 0) nb_p2++;
    }
    //printf("\n");
  }
  //printf("update score\n");
  update_white_score(&nb_p1);
  update_black_score(&nb_p2);
  return FALSE;
}

static void player_move(GtkWidget *p_case)
{
  if (*(state->play) == 0) return;

  int col, row, n;
  char msg[MAXDATASIZE] = "";

  // Traduction coordonnees damier en indexes matrice damier
  coord_to_indexes(gtk_buildable_get_name(GTK_BUILDABLE(gtk_bin_get_child(GTK_BIN(p_case)))), &col, &row);

  sprintf(msg, "GAME:%d:MOVE:%d:%d-%d", game_id, couleur, row, col);

  n = send(*(state->sockfd), msg, strlen(msg), 0);
  printf(">> [%d bytes] %s\n", n, msg);
}

static void server_connect(GtkWidget *b)
{
  login = get_login();

  if (strlen(login) < 1) return;

  //printf("Connection button triggered\n");

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("An error occured while trying to create a socket : ");
    return;
  }

  memset(&server, 0, sizeof(server));

  server.sin_family = AF_INET;
  server.sin_port = htons(atoi(get_server_port()));
  server.sin_addr.s_addr = inet_addr(get_server_address());

  //printf("Connecting to server...\n");

  if (connect(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    //printf("An error occured while trying to connect to the server\n");
    return;
  }

  //printf("Connected !\n");
  disable_server_connect();

  (*state).sockfd = &sockfd;
  (*state).play = (int *) malloc(sizeof(int));
  *(*state).play = -1;

  pthread_create(&read_thread, NULL, t_read, (void *) state);

  signup(login);

  return;
}

static void start_game(GtkWidget *b)
{
  printf("start\n");
  char message[32];
  int n;
  if(game_id < 0)
  {
    // Recuperation  adresse et port adversaire au format chaines caracteres
    target_name=get_target();
  
    sprintf(message, "GAME:INVITE:%s", target_name);
    n = send(sockfd, message, sizeof(message), 0);
    printf(">> [%d bytes] : %s\n", n, message);
    memset(message, 0, sizeof(message));
  }
}

static void spectate(GtkWidget *b)
{
  char message[32];
  int n;
  if (game_id == -1)
  {
    target_name = get_target();
    sprintf(message, "GAME:SPECTATE:%s", target_name);
  }
  else
  {
    sprintf(message, "GAME:UNSPECTATE:%d", game_id);
    game_id = -1;
    for (int i = 0; i < 8; i++) {
      for (int j = 0; j < 8; j++) {
        damier[i][j] = -1;
        change_img_case(i, j, -1);
      }
    }
  }
  n = send(sockfd, message, sizeof(message), 0);
  printf(">> [%d bytes] : %s\n", n, message);
  memset(message, 0, sizeof(message));
}

static void clear_game(GtkWidget *b)
{
  game_id = -1;
  target_name = NULL;
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      if (damier[i][j] != -1) {
        damier[i][j] = -1;
        change_img_case(i, j, -1);
      }
    }

}

static void forfeit_game(GtkWidget *b)
{
  if (game_id == -1) return;
  char message[32];
  int n;
  game_id = -1;
  target_name = NULL;
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      if (damier[i][j] != -1) {
        damier[i][j] = -1;
        change_img_case(i, j, -1);
      }
    }
  }

  sprintf(message, "%s", "GAME:FORFEIT");
  n = send(sockfd, message, sizeof(message), 0);
  printf("\n>> [%d bytes] : %s\n", n, message);
}

gboolean heart()
{
  printf("hello\n");
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      damier[i][j] = 2;
    }

  damier[0][0] = -1;
  damier[1][0] = -1;
  damier[3][0] = -1;
  damier[4][0] = -1;
  damier[6][0] = -1;
  damier[7][0] = -1;
  damier[0][1] = -1;
  damier[7][1] = -1;
  damier[0][5] = -1;
  damier[7][5] = -1;
  damier[0][6] = -1;
  damier[1][6] = -1;
  damier[6][6] = -1;
  damier[7][6] = -1;
  damier[0][7] = -1;
  damier[1][7] = -1;
  damier[2][7] = -1;
  damier[5][7] = -1;
  damier[6][7] = -1;
  damier[7][7] = -1;

  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      change_img_case(i, j, damier[i][j]);
    }
  }
  return FALSE;
}

int main (int argc, char ** argv)
{
  state = (State *) malloc(sizeof(State));   
  XInitThreads();
   
  /* Initialisation de GTK+ */
  gtk_init (& argc, & argv);
   
  /* Creation d'un nouveau GtkBuilder */
  p_builder = gtk_builder_new();
 
  if (p_builder != NULL)
  {
    /* Chargement du XML dans p_builder */
    gtk_builder_add_from_file (p_builder, "UI_Glade/Othello.glade", & p_err);
 
    if (p_err == NULL)
    {
      /* Recuparation d'un pointeur sur la fenetre. */
      GtkWidget * p_win = (GtkWidget *) gtk_builder_get_object (p_builder, "window1");

      /* Gestion evenement clic pour chacune des cases du damier */

      char id[11];
      for (int i = 1; i <= 8; i++)
      {
        for (int j = 0; j < 8; j++)
        {
          sprintf(id, "eventbox%c%d", (char) (65 + j), i);
          g_signal_connect(gtk_builder_get_object(p_builder, id), "button_press_event", G_CALLBACK(player_move), NULL);
        }
      }
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "spectate"), FALSE);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "forfeit"), FALSE);

      g_signal_connect(gtk_builder_get_object(p_builder, "button_connect"), "clicked", G_CALLBACK(server_connect), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "button_start"), "clicked", G_CALLBACK(start_game), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "spectate"), "clicked", G_CALLBACK(spectate), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "forfeit"), "clicked", G_CALLBACK(forfeit_game), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "clear"), "clicked", G_CALLBACK(clear_game), NULL);

      /* Gestion clic bouton fermeture fenetre */
      g_signal_connect_swapped(G_OBJECT(p_win), "destroy", G_CALLBACK(gtk_main_quit), NULL);
      gtk_widget_show_all(p_win);
      gtk_main();
    }
    else
    {
      g_error ("%s", p_err->message);
      g_error_free (p_err);
    }
  }

  pthread_join(read_thread, NULL);
  gtk_main_quit();
 
  return EXIT_SUCCESS;
}
