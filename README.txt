Compile and run server :

```
gcc src/server.c -o dst/srv -lpthread && ./dst/srv
```

Compile and run client :

```
gcc src/othello_GUI.c -o dst/othello_GUI -lX11 -Wall $(pkg-config --cflags --libs gtk+-3.0) && ./dst/othello_GUI
```
