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
============ THREAD SAFE INTERFACE UPDATE FUNCTION ============
*/

gboolean disable_button_start(gpointer data);
gboolean init_game_interface(gpointer data);
gboolean update_white_label(gpointer data);
gboolean update_black_label(gpointer data);
gboolean update_white_score(gpointer data);
gboolean update_black_score(gpointer data);
gboolean update_title(gpointer data);
gboolean update_move(gpointer data);
gboolean count_score(gpointer data);

#define MAXDATASIZE 256

// Entetes des fonctions  

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J1(void);

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J2(void);

/* Fonction transformant coordonnees du damier graphique en indexes pour matrice du damier */
void coord_to_indexes(const gchar *coord, int *col, int *lig);

void indexes_to_coord(int col, int lig, char * coord);

/* Fonction appelee lors du clique sur une case du damier */
static void player_move(GtkWidget *p_case);

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *get_server_address(void);

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *get_server_port(void);

/* Fonction retournant texte du champs login de l'interface graphique */
char *get_login(void);

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *read_target(void);

/* Fonction appelee lors du clique du bouton Se connecter */
static void server_connect(GtkWidget *b);

/* Fonction desactivant bouton demarrer partie */
void disable_server_connect(void);

/* Fonction appelee lors du clique du bouton Demarrer partie */
static void start_game(GtkWidget *b);

/* Fonction desactivant les cases du damier */
void gele_damier(void);

/* Fonction activant les cases du damier */
void degele_damier(void);


/* Fonction reinitialisant la liste des joueurs sur l'interface graphique */
void reset_liste_joueurs(void);

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
void affich_joueur_buffer(char *login);

/* Fonction affichant boite de dialogue si partie gagnee */
gboolean affiche_fenetre_fin(gpointer data);

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j);

typedef struct State State;
typedef struct Move Move;

struct State {
  int * sockfd;
  int * play;
};

struct Move {
  int col;
  int row;
  int player;
};

/* Variables globales */
int damier[8][8]; // tableau associe au damier
int couleur;    // 0 : pour noir, 1 : pour blanc

char * login;

State * state;

int game_id = -1;

pthread_t read_thread;

int port;   // numero port passé lors de l'appel

char * addr_j2, * port_j2;  // Info sur adversaire
char * target_name;

pthread_t thr_id; // Id du thread fils gerant connexion socket

int sockfd; // descripteurs de socket
struct sockaddr_in server;

int serverSize = sizeof(server);

/* Variables globales associées à l'interface graphique */
GtkBuilder  *  p_builder   = NULL;
GError      *  p_err       = NULL;


void signup(char * login);
void get_users(char message[]);

// Thread de lecture

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
      { // Received list of connected players
        get_users(buffer);
      }
      else // not updated user list
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
              g_main_context_invoke(main_context, (GSourceFunc) disable_button_start, p_builder);

              // Game ID to send back to the server
              token = strtok(NULL, ":");
              if (token != NULL)
              {
                game_id = strtol(token, NULL, 10);

                // Player's position (0 for black, 1 for white)
                token = strtok(NULL, ":");
                couleur = (int) strtol(token, NULL, 10);

                // Safely initialize the game interface in the main thread
                g_main_context_invoke(main_context, (GSourceFunc) init_game_interface, damier);

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
                  g_main_context_invoke(main_context, (GSourceFunc) update_title, play);                  //printf("New window title is set\n");
                  *(st->play) = 1;
                }
              }
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
                if (strcmp(token, "PLAY") && strcmp(token, "WAIT"))
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
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez gagné !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, won_message);
            }
            // Game ends with the losing message
            else if (token != NULL && strcmp(token, "LOST") == 0)
            {
              *(st->play) = 0;
              //affiche_fenetre_fin("Fin de la partie.\n\nVous avez perdu !");
              g_main_context_invoke(main_context, (GSourceFunc) affiche_fenetre_fin, lost_message);
            }
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

gboolean update_title(gpointer data)
{
  GtkWidget * p_win = (GtkWidget *) gtk_builder_get_object (p_builder, "window1");
  char * text = (char *) data;
  gtk_window_set_title((GtkWindow *) p_win, text);
  return FALSE;
}

gboolean update_move(gpointer data)
{
  // gdk_threads_enter();
  Move * m = (Move *) data;
  int col, row, player;
  col = m->col;
  row = m->row;
  player = m->player;

  char * coord;

  coord=malloc(3*sizeof(char));

  indexes_to_coord(col, row, coord);

  if(player)
  { // image pion blanc
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_blanc.png");
  }
  else
  { // image pion noir
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_noir.png");
  }
  free(m);
  // gdk_threads_leave();
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
void coord_to_indexes(const gchar *coord, int *col, int *lig)
{
  char *c;
  
  c=malloc(3*sizeof(char));
  
  c=strncpy(c, coord, 1);
  c[1]='\0';

  *col = ((int) c[0] - 65);
  
  *lig=atoi(coord+1)-1;
}

/* Fonction transforme coordonnees du damier graphique en indexes pour matrice du damier */
void indexes_to_coord(int col, int lig, char *coord)
{
  char c;

  //printf("indexes_to_coord %d - %d \n", lig, col);

  c=(char) ('A' + col % 26);

  //printf("sprintf coord\n");
  sprintf(coord, "%c%d", c, lig+1);
  //printf("end indexes_to_coord %s\n", coord);
}

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j)
{
  //printf("start change_img_case\n");
  char * coord;

  coord=malloc(3*sizeof(char));

  //printf("coord malloc\n");

  indexes_to_coord(col, lig, coord);

  //printf("set img\n");
  if(couleur_j)
  { // image pion blanc
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_blanc.png");
  }
  else
  { // image pion noir
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_noir.png");
  }
}

/* Fonction permettant changer nom joueur blanc dans cadre Score */
void set_label_J1(char *text)
{
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J1")), text);
}

/* Fonction permettant de changer nom joueur noir dans cadre Score */
void set_label_J2(char *text)
{
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J2")), text);
}

/* Fonction permettant de changer score joueur blanc dans cadre Score */
void set_score_J1(int score)
{
  //printf("set_score_J1\n");
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d", score);
  
  //printf("setting label for j1\n");
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ1")), s);
  //printf("set_score_J1 end\n");
}

/* Fonction permettant de changer score joueur noir dans cadre Score */
void set_score_J2(int score)
{
  //printf("set_score_J2\n");
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d", score);
  
  //printf("setting label for j2\n");
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ2")), s);
  //printf("set_score_J2 end\n");
}

/* Fonction appelee lors du clique sur une case du damier */
static void player_move(GtkWidget *p_case)
{
  if (*(state->play) == 0) return;

  int col, lig, n;
  char msg[MAXDATASIZE] = "";

  // Traduction coordonnees damier en indexes matrice damier
  coord_to_indexes(gtk_buildable_get_name(GTK_BUILDABLE(gtk_bin_get_child(GTK_BIN(p_case)))), &col, &lig);

  sprintf(msg, "GAME:%d:MOVE:%d:%d-%d", game_id, couleur, lig, col);

  n = send(*(state->sockfd), msg, strlen(msg), 0);
  printf(">> [%d bytes] %s\n", n, msg);
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
char *read_target(void)
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

/* Fonction appelee lors du clique du bouton Se connecter */
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

void signup(char * login)
{
  int n;
  char message[5 + strlen(login)];
  sprintf(message, "NAME:%s", login);
  n = send(sockfd, message, strlen(message), 0);
  //printf("\n>> [%d bytes] : %s\n", n, message);
}

void get_users(char * message)
{
  reset_liste_joueurs();
  char * token;
  do
  {
    token = strtok(NULL, "\n");
    if (token != NULL && strlen(token) >= 1)
    {
      affich_joueur_buffer(token);
    }
  } while (token != NULL);
}

/* Fonction desactivant bouton demarrer partie */
void disable_server_connect(void)
{
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), TRUE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_connect"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_adr"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_port"), FALSE);
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_login"), FALSE);
}

/* Fonction desactivant bouton demarrer partie */
gboolean disable_button_start(gpointer data)
{
  GtkBuilder * p_builder = (GtkBuilder *) data;
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
  return FALSE;
}

/* Fonction traitement signal bouton Demarrer partie */
static void start_game(GtkWidget *b)
{
  char message[32];
  int n;
  if(game_id==-1)
  {
    // Deactivation bouton demarrer partie
    gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
    
    // Recuperation  adresse et port adversaire au format chaines caracteres
    target_name=read_target();
  
    sprintf(message, "GAME:NEW:%s:%s", login, target_name);
    n = send(sockfd, message, sizeof(message), 0);
    //printf("\n>> [%d bytes] : %s\n", n, message);
  }
}

/* Fonction desactivant les cases du damier */
void gele_damier(void)
{
  char id[11];
  for (int i = 1; i <= 8; i++) {
    for (int j = 0; j < 8; j++) {
      sprintf(id, "eventbox%c%d", (char) (65 + j), i);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object(p_builder, id), FALSE);
    }
  }
}

/* Fonction activant les cases du damier */
void degele_damier(void)
{
  char id[11];
  for (int i = 1; i <= 8; i++) {
    for (int j = 0; j < 8; j++) {
      sprintf(id, "eventbox%c%d", (char) (65 + j), i);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object(p_builder, id), TRUE);
    }
  }
}

/* Fonction permettant d'initialiser le plateau de jeu */
gboolean init_game_interface(gpointer data)
{
printf("hello\n");
  int (*damier)[8] = (int(*)[8])data;
  char * text_you = g_strdup("You");
  char * text_opponent = g_strdup("Opponent");
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
  if(couleur==1)
  {
    update_white_label(text_you);
    update_black_label(text_opponent);
  }
  else
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
void reset_liste_joueurs(void)
{
  GtkTextIter start, end;
  
  gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start);
  gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &end);
  
  gtk_text_buffer_delete(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), &start, &end);
}

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
void affich_joueur_buffer(char *login)
{
  const gchar *joueur;
  
  joueur=g_strconcat(login, "\n", NULL);
  
  gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(p_builder, "textview_joueurs")))), joueur, strlen(joueur));
}

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J1(void)
{
  const gchar *c;

  c=gtk_label_get_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ1")));

  return atoi(c);
}

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J2(void)
{
  const gchar *c;

  c=gtk_label_get_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ2")));

  return atoi(c);
}

gboolean count_score(gpointer data)
{
  // gdk_threads_enter();
  int (*damier)[8] = (int(*)[8])data;
  int nb_p1 = 0;
  int nb_p2 = 0;
  //printf("Counting scores\n");
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      if (damier[i][j] == 1) nb_p1++;
      else if (damier[i][j] != -1) nb_p2++;
    }
  }
  //printf("Scores are white: %d\t\tblack:%d\n", nb_p1, nb_p2);
  //printf("Calling set_score_J1\n");
  update_white_score(&nb_p1);
  //printf("Calling set_score_J2\n");
  update_black_score(&nb_p2);
  //printf("End count_score\n");
//   gdk_threads_leave();
  return FALSE;
}

int main (int argc, char ** argv)
{
  int i, j;

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
  

      g_signal_connect(gtk_builder_get_object(p_builder, "button_connect"), "clicked", G_CALLBACK(server_connect), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "button_start"), "clicked", G_CALLBACK(start_game), NULL);

      /* Gestion clic bouton fermeture fenetre */
      g_signal_connect_swapped(G_OBJECT(p_win), "destroy", G_CALLBACK(gtk_main_quit), NULL);
          
      /* Initialisation du damier de jeu */
      for(i=0; i<8; i++)
      {
        for(j=0; j<8; j++)
        {
          damier[i][j]=-1; 
        }  
      }

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
