# Makefile per il proxy

# Dichiarazione folders
INTERFACES = ../ext
HEADERS = ../header

# Compilazione/Linking
GCC = gcc
GCCFLAGS = -Wall -Wunused -ansi -pedantic -pg
LINKERFLAGS = -lpthread -lm


# Obiettivi

all: proxy

proxy: proxy.o prefetching.o caching.o util.o
	$(GCC) -o proxy ${GCCFLAGS} ${LINKERFLAGS} proxy.o prefetching.o caching.o util.o
	
proxy.o: proxy.c
	$(GCC) -c $(GCCFLAGS) -I $(INTERFACES) -I $(HEADERS) proxy.c

prefetching.o: prefetching.c
	$(GCC) -c $(GCCFLAGS) -I $(INTERFACES) -I $(HEADERS) prefetching.c

caching.o: caching.c
	$(GCC) -c $(GCCFLAGS) -I $(INTERFACES) -I $(HEADERS) caching.c

util.o: util.c
	$(GCC) -c $(GCCFLAGS) -I $(INTERFACES) -I $(HEADERS) util.c


# Pulizia

clean:	
	rm -f *.o
	rm -f proxy
