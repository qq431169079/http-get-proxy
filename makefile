CC = gcc $(CFLAGS)
OBJ = tcp.o proxy.o http.o url.o slice.o tprintf.o
LIB = -lpthread
CFLAGS = -g

all: webproxy clean

webproxy: $(OBJ) webproxy.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIB)

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm $(OBJ)
