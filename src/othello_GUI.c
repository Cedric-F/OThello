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

#include <signal.h>
#include <sys/signalfd.h>

#include <gtk/gtk.h>


#define MAXDATASIZE 256

/* Fonction desactivant les cases du damier */
void gele_damier(void);

/* Fonction activant les cases du damier */
void degele_damier(void);

/* Fonction permettant d'initialiser le plateau de jeu */
void init_interface_jeu(void);

/* Fonction reinitialisant la liste des joueurs sur l'interface graphique */
void reset_liste_joueurs(void);

/* Fonction permettant d'ajouter un joueur dans la liste des joueurs sur l'interface graphique */
void affich_joueur_buffer(char *login);

/* Fonction affichant boite de dialogue si partie gagnee */
void affiche_fenetre_gagne(void);

/* Fonction affichant boite de dialogue si partie perdue */
void affiche_fenetre_perdu(void);
  
/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j);

typedef struct State State;

struct State {
  int * sockfd;
  int * play;
};

int startsWith( const char * theString, const char * theBase ) {
  return strncmp( theString, theBase, strlen( theBase ) ) == 0;
}

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
  
  int sockfd, newsockfd=-1; // descripteurs de socket
  int addr_size;   // taille adresse
  struct sockaddr *their_addr;  // structure pour stocker adresse adversaire
  struct sockaddr_in server;

  int serverSize = sizeof(server);

  fd_set master, read_fds, write_fds; // ensembles de socket pour toutes les sockets actives avec select
  int max_sd;     // utilise pour select

/* Variables globales associées à l'interface graphique */
  GtkBuilder  *  p_builder   = NULL;
  GError      *  p_err       = NULL;


void signup(char * login);
void get_users(char message[]);

// Thread de lecture

void * t_read(void * state)
{
  State * st = (State *) state;
  char buffer[MAXDATASIZE];
  char list[17] = "Connected users:\n";
  char * token;
  ssize_t size;
  while(1)
  {
    memset(buffer, 0, sizeof(buffer));
    size = read(*(st->sockfd), buffer, MAXDATASIZE);
    if (size == 0)
    {
      printf("\nLost connection to server...\n");
      exit(EXIT_FAILURE);
    }
    else if (size >= 1)
    {
      fflush(stdout);
      printf("\n<< [%ld bytes] %s\n", size, buffer);
      if (startsWith(buffer, list))
      {
        get_users(buffer);
      } else // not updated user list
      {
        token = strtok(buffer, ":");
        if (token != NULL)
        {
          printf("%s\n", token);
          if (strcmp(token, "GAME") == 0) // game related data
          {
            token = strtok(NULL, ":");
            if (token != NULL && strcmp(token, "NEW") == 0) // new game
            {
              token = strtok(NULL, ":"); // Game id
              if (token != NULL)
              {
                game_id = strtol(token, NULL, 10);
                token = strtok(NULL, ":");
                couleur = (int) strtol(token, NULL, 10);
                init_interface_jeu();
                token = strtok(NULL, "\0");
                if (strcmp(token, "WAIT") == 0)
                {
                  *(st->play) = 0;
                  gele_damier();
                }
                else if (strcmp(token, "PLAY") == 0)
                {
                  *(st->play) = 1;
                  degele_damier();
                }
              }
            }
            else if (token != NULL && strcmp(token, "MOVE") == 0)
            {
              token = strtok(NULL, ":");
              if (token != NULL)
              {
                int move = (int) strtol(token, NULL, 10);
                int col, lig;
                col = (int) strtol(strtok(NULL, "-"), NULL, 10);
                lig = (int) strtol(strtok(NULL, ":"), NULL, 10);
                printf("%d placed at %d %d\n", move, col, lig);
                change_img_case(col, lig, move);
                token = strtok(NULL, "\0");
                if (strcmp(token, "WAIT") == 0)
                {
                  *(st->play) = 0;
                  gele_damier();
                }
                else if (strcmp(token, "PLAY") == 0)
                {
                  *(st->play) = 1;
                  degele_damier();
                }
              }
            }
            else if (token != NULL && strcmp(token, "WIN"))
            {
              affiche_fenetre_gagne();
            }
            else if (token != NULL && strcmp(token, "LOST"))
            {
              affiche_fenetre_perdu();
            }
          }
        }
      }
    }
    else if (size < 0)
    {
      printf("Some erreur with read\n");
      exit(EXIT_FAILURE);
    }
  }
  exit(EXIT_SUCCESS);
}

// Entetes des fonctions  

/* Fonction permettant changer nom joueur blanc dans cadre Score */
void set_label_J1(char *texte);

/* Fonction permettant de changer nom joueur noir dans cadre Score */
void set_label_J2(char *texte);

/* Fonction permettant de changer score joueur blanc dans cadre Score */
void set_score_J1(int score);

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J1(void);

/* Fonction permettant de changer score joueur noir dans cadre Score */
void set_score_J2(int score);

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J2(void);

/* Fonction transformant coordonnees du damier graphique en indexes pour matrice du damier */
void coord_to_indexes(const gchar *coord, int *col, int *lig);

/* Fonction appelee lors du clique sur une case du damier */
static void coup_joueur(GtkWidget *p_case);

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *lecture_addr_serveur(void);

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *lecture_port_serveur(void);

/* Fonction retournant texte du champs login de l'interface graphique */
char *lecture_login(void);

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *read_target_name(void);

/* Fonction appelee lors du clique du bouton Se connecter */
static void clique_connect_serveur(GtkWidget *b);

/* Fonction desactivant bouton demarrer partie */
void disable_button_start(void);

/* Fonction appelee lors du clique du bouton Demarrer partie */
static void clique_connect_adversaire(GtkWidget *b);

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

  c=(char) (65 + col);
    
  sprintf(coord, "%c%d\0", c, lig+1);
}

/* Fonction permettant de changer l'image d'une case du damier (indiqué par sa colonne et sa ligne) */
void change_img_case(int col, int lig, int couleur_j)
{
  printf("début change_img_case\n");
  char * coord;
  
  coord=malloc(3*sizeof(char));

  printf("avant index to coord\n");

  indexes_to_coord(col, lig, coord);

  printf("après index to coord\n");

  if(couleur_j)
  { // image pion blanc
    printf("white\n");
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_blanc.png");
    printf("after white\n");
  }
  else
  { // image pion noir
    printf("black\n");
    gtk_image_set_from_file(GTK_IMAGE(gtk_builder_get_object(p_builder, coord)), "UI_Glade/case_noir.png");
    printf("after black\n");
  }
}

/* Fonction permettant changer nom joueur blanc dans cadre Score */
void set_label_J1(char *texte)
{
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J1")), texte);
}

/* Fonction permettant de changer nom joueur noir dans cadre Score */
void set_label_J2(char *texte)
{
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_J2")), texte);
}

/* Fonction permettant de changer score joueur blanc dans cadre Score */
void set_score_J1(int score)
{
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d\0", score);
  
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ1")), s);
}

/* Fonction permettant de récupérer score joueur blanc dans cadre Score */
int get_score_J1(void)
{
  const gchar *c;
  
  c=gtk_label_get_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ1")));
  
  return atoi(c);
}

/* Fonction permettant de changer score joueur noir dans cadre Score */
void set_score_J2(int score)
{
  char *s;
  
  s=malloc(5*sizeof(char));
  sprintf(s, "%d\0", score);
  
  gtk_label_set_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ2")), s);
}

/* Fonction permettant de récupérer score joueur noir dans cadre Score */
int get_score_J2(void)
{
  const gchar *c;
  
  c=gtk_label_get_text(GTK_LABEL(gtk_builder_get_object (p_builder, "label_ScoreJ2")));
  
  return atoi(c);
}

/* Fonction appelee lors du clique sur une case du damier */
static void coup_joueur(GtkWidget *p_case)
{
  if (*(state->play) == 0) return;

  int col, lig, type_msg, nb_piece, score, n;
  char buf[MAXDATASIZE];
  char msg[MAXDATASIZE];
  
  // Traduction coordonnees damier en indexes matrice damier
  coord_to_indexes(gtk_buildable_get_name(GTK_BUILDABLE(gtk_bin_get_child(GTK_BIN(p_case)))), &col, &lig);

  sprintf(buf, "GAME:%d:MOVE:", game_id);
  sprintf(msg, "%s", buf);
  sprintf(buf, "%d:", couleur);
  strcat(msg, buf);
  sprintf(buf, "%d-", lig);
  strcat(msg, buf);
  sprintf(buf, "%d\0", col);
  strcat(msg, buf);

  n = send(*(state->sockfd), msg, sizeof(msg), 0);
  printf(">> [%d bytes] : %s\n", n, msg);
}

/* Fonction retournant texte du champs adresse du serveur de l'interface graphique */
char *lecture_addr_serveur(void)
{
  GtkWidget *entry_addr_srv;
  
  entry_addr_srv = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_adr");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_addr_srv));
}

/* Fonction retournant texte du champs port du serveur de l'interface graphique */
char *lecture_port_serveur(void)
{
  GtkWidget *entry_port_srv;
  
  entry_port_srv = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_port");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_port_srv));
}

/* Fonction retournant texte du champs login de l'interface graphique */
char *lecture_login(void)
{
  GtkWidget *entry_login;
  
  entry_login = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_login");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_login));
}

/* Fonction retournant texte du champs adresse du cadre Joueurs de l'interface graphique */
char *read_target_name(void)
{
  GtkWidget * entry_target_name;
  
  entry_target_name = (GtkWidget *) gtk_builder_get_object(p_builder, "entry_target_name");
  
  return (char *)gtk_entry_get_text(GTK_ENTRY(entry_target_name));
}

/* Fonction affichant boite de dialogue si partie gagnee */
void affiche_fenetre_gagne(void)
{
  GtkWidget *dialog;
    
  GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;
  
  dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_builder_get_object(p_builder, "window1")), flags, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Fin de la partie.\n\n Vous avez gagné!!!");
  gtk_dialog_run(GTK_DIALOG (dialog));
  
  gtk_widget_destroy(dialog);
}

/* Fonction affichant boite de dialogue si partie perdue */
void affiche_fenetre_perdu(void)
{
  GtkWidget *dialog;
    
  GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;
  
  dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_builder_get_object(p_builder, "window1")), flags, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Fin de la partie.\n\n Vous avez perdu!");
  gtk_dialog_run(GTK_DIALOG (dialog));
  
  gtk_widget_destroy(dialog);
}

/* Fonction appelee lors du clique du bouton Se connecter */
static void clique_connect_serveur(GtkWidget *b)
{
  login = lecture_login();

  if (strlen(login) < 1) return;

  printf("Connection button triggered\n");

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("An error occured while trying to create a socket : ");
    return;
  }

  memset(&server, 0, sizeof(server));

  server.sin_family = AF_INET;
  server.sin_port = htons(atoi(lecture_port_serveur()));
  server.sin_addr.s_addr = inet_addr(lecture_addr_serveur());

  printf("Connecting to server...\n");

  if (connect(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    printf("An error occured while trying to connect to the server\n");
    return;
  }

  //FD_ZERO(&read_fds);
  //FD_SET(sockfd, &read_fds);

  printf("Connected !\n");
  disable_server_connect();

  (*state).sockfd = &sockfd;
  (*state).play = (int *) malloc(sizeof(int));
  *(*state).play = -1;

  printf("%p - %d\n", (*state).play, *(*state).play);
  pthread_create(&read_thread, NULL, t_read, (void *) state);

  signup(login);

  return;
}

void signup(char * login)
{
  int n;
  char message[5 + strlen(login)];
  strcat(message, "NAME ");
  strcat(message, login);
  strcat(message, "\0");
  n = send(sockfd, message, sizeof(message), 0);
  printf("\n>> [%d bytes] : %s\n", n, message);
}

void get_users(char * message)
{
  reset_liste_joueurs();
  char * token;
  token = strtok(message, "\n");
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
void disable_button_start(void)
{
  gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
}

/* Fonction traitement signal bouton Demarrer partie */
static void clique_connect_adversaire(GtkWidget *b)
{
  char message[32];
  int n;
  if(game_id==-1)
  {
    // Deactivation bouton demarrer partie
    gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
    
    // Recuperation  adresse et port adversaire au format chaines caracteres
    target_name=read_target_name();
  
    sprintf(message, "GAME:NEW:%s:", login);
    strcat(message, target_name);
    strcat(message, "\0");
    n = send(sockfd, message, sizeof(message), 0);
    printf("\n>> [%d bytes] : %s\n", n, message);
  }
}

/* Fonction desactivant les cases du damier */
void gele_damier(void)
{
  char * id = (char *) malloc(sizeof(char) * 11);
  for (int i = 0; i <= 8; i++) {
    for (int j = 0; j < 8; j++) {
      sprintf(id, "eventbox%c%d", (char) (65 + j), i);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object(p_builder, id), FALSE);
    }
  }
  free(id);
}

/* Fonction activant les cases du damier */
void degele_damier(void)
{
  char * id = (char *) malloc(sizeof(char) * 11);
  for (int i = 0; i <= 8; i++) {
    for (int j = 0; j < 8; j++) {
      sprintf(id, "eventbox%c%d", (char) (65 + j), i);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object(p_builder, id), TRUE);
    }
  }
  free(id);
}

/* Fonction permettant d'initialiser le plateau de jeu */
void init_interface_jeu(void)
{
  // Initilisation du damier (D4=blanc, E4=noir, D5=noir, E5=blanc)
  change_img_case(3, 3, 1);
  change_img_case(4, 3, 0);
  change_img_case(3, 4, 0);
  change_img_case(4, 4, 1);
  
  // Initialisation des scores et des joueurs
  if(couleur==1)
  {
    set_label_J1("Vous");
    set_label_J2("Adversaire");
  }
  else
  {
    set_label_J1("Adversaire");
    set_label_J2("Vous");
  }

  set_score_J1(2);
  set_score_J2(2);
  
  /***** TO DO *****/
  
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

/* Fonction exécutée par le thread gérant les communications à travers la socket */
static void * f_com_socket(void *p_arg)
{
  int i, nbytes, col, lig;
  
  char buf[MAXDATASIZE], *tmp, *p_parse;
  int len, bytes_sent, t_msg_recu;

  sigset_t signal_mask;
  int fd_signal;
  
  uint16_t type_msg, col_j2;
  uint16_t ucol, ulig;
  
  /* Association descripteur au signal SIGUSR1 */
  sigemptyset(&signal_mask);
  sigaddset(&signal_mask, SIGUSR1);
    
  if(sigprocmask(SIG_BLOCK, &signal_mask, NULL) == -1)
  {
    printf("[Port joueur %d] Erreur sigprocmask\n", port);
    
    return 0;
  }
    
  fd_signal = signalfd(-1, &signal_mask, 0);
    
  if(fd_signal == -1)
  {
    printf("[Port joueur %d] Erreur signalfd\n", port);

    return 0;
  }

  /* Ajout descripteur du signal dans ensemble de descripteur utilisé avec fonction select */
  FD_SET(fd_signal, &master);
  
  if(fd_signal>max_sd)
  {
    max_sd=fd_signal;
  }

  
  while(1)
  {
    read_fds=master;  // copie des ensembles
    
    if(select(max_sd+1, &read_fds, &write_fds, NULL, NULL)==-1)
    {
      perror("Problème avec select");
      exit(4);
    }
    
    printf("[Port joueur %d] Entree dans boucle for\n", port);
    for(i=0; i<=max_sd; i++)
    {
      printf("[Port joueur %d] newsockfd=%d, iteration %d boucle for\n", port, newsockfd, i);

      if(FD_ISSET(i, &read_fds))
      {
        if(i==fd_signal)
        {
          /* Cas où de l'envoie du signal par l'interface graphique pour connexion au joueur adverse */
          
          
          /***** TO DO *****/
          
        }
      
        if(i==sockfd)
        { // Acceptation connexion adversaire
    
      
          /***** TO DO *****/
      
          gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
        }
        else
        { // Reception et traitement des messages du joueur adverse
      
      
          /***** TO DO *****/

      
        }
      }
    }
  }
  
  return NULL;
}


int main (int argc, char ** argv)
{
  int i, j, ret;

  if(argc!=2)
  {
    printf("\nPrototype : ./othello num_port\n\n");

    exit(1);
  }
  state = (State *) malloc(sizeof(State));   
   
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

      char * id = (char *) malloc(sizeof(char) * 11);
      for (int i = 0; i <= 8; i++)
      {
        for (int j = 0; j < 8; j++)
        {
          sprintf(id, "eventbox%c%d", (char) (65 + j), i);
          g_signal_connect(gtk_builder_get_object(p_builder, id), "button_press_event", G_CALLBACK(coup_joueur), NULL);
        }
      }
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "entry_port_j2"), FALSE);
      gtk_widget_set_sensitive((GtkWidget *) gtk_builder_get_object (p_builder, "button_start"), FALSE);
      free(id);
      /* Gestion clic boutons interface */
      g_signal_connect(gtk_builder_get_object(p_builder, "button_connect"), "clicked", G_CALLBACK(clique_connect_serveur), NULL);
      g_signal_connect(gtk_builder_get_object(p_builder, "button_start"), "clicked", G_CALLBACK(clique_connect_adversaire), NULL);

      /* Gestion clic bouton fermeture fenetre */
      g_signal_connect_swapped(G_OBJECT(p_win), "destroy", G_CALLBACK(gtk_main_quit), NULL);
         
         
         
      /* Recuperation numero port donne en parametre */
      port=atoi(argv[1]);
          
      /* Initialisation du damier de jeu */
      for(i=0; i<8; i++)
      {
        for(j=0; j<8; j++)
        {
          damier[i][j]=-1; 
        }  
      }

     
      /***** TO DO *****/
       
      // Initialisation socket et autres objets, et création thread pour communications avec joueur adverse
     
 
      gtk_widget_show_all(p_win);
      gtk_main();
    }
    else
    {
      /* Affichage du message d'erreur de GTK+ */
      g_error ("%s", p_err->message);
      g_error_free (p_err);
    }
  }

  pthread_join(&t_read, NULL);
 
  return EXIT_SUCCESS;
}
