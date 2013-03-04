/************************************************************************

	Copyright (C) Alain Di Chiappari	2011
	alain.dichiappari@studio.unibo.it

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

************************************************************************/

/* 
	prefetching.c 

	Modulo contenente la funzione principale 
	per l'esecuzione dei thread di prefetching.

*/

#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <math.h>
#include <dirent.h>

#include "caching.e"
#include "util.e"
#include "proxy.e"


/* 
	Prende già tutte le informazioni per stabilire una sessione di richiesta/risposta con 
	un server. Una volta scaricate le salva in cache con nome univoco e associate ad una struttura
	di file_cache.

	NB: 
	-USO CAMPO "buf[]" della struttura Request per l'URL
	-Se mi chiamano avendo preso REF è 1° livello, se da IDX;REF è di 2° livello 
*/
void *get_and_cache(void *p) {

	int ris;
	int servfd;
	char tmpname[MAXLENPATH];
	int prefetch_level;
	char buffer[MAXLENRESP];
	int buffer_len;
	
	struct sockaddr_in Local, Serv;

	/* Parametro è un puntatore ad una struttura Request nello heap */
	struct Request *p_prefetch_req = (struct Request *)p;

	/* Controllo sul numero dei thread */
	pthread_mutex_lock(&mutex_num);
	if(numthread < MAXNUMTHREADWORKING)
		numthread++;
	pthread_mutex_unlock(&mutex_num);
	if(numthread >= MAXNUMTHREADWORKING) {
		exitThread();
	return(NULL);
	}

	/* Prendo i valori che mi servono */
	uint16_t port = p_prefetch_req->port;
	char URL[MAXLENREQ];
	strncpy(URL, p_prefetch_req->buf, sizeof(p_prefetch_req->buf));
	char path[MAXLENPATH];
	strncpy(path, p_prefetch_req->path, sizeof(p_prefetch_req->path));
	struct in_addr inaddr;
	memcpy(&inaddr, &(p_prefetch_req->inaddr), sizeof(struct in_addr));
	prefetch_level = p_prefetch_req->reqType;

	/* Libero spazio su heap */
	free(p);

	/* Per evitare segmentation fault */
	p = NULL;
	p_prefetch_req = NULL;

	/* Tentativi connessione server */
	int servconn_chance = SERVER_CHANCE;

	while (1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While get_and_cache.\n", pthread_self());
fflush(stdout);
#endif

		/* Controllo se qualcun'altro nel frattempo ha già messo in cache lo stesso file */
		if(get_cache_name(&inaddr, port, path, tmpname)) {
			exitThread();
		}

		servfd = socket(AF_INET, SOCK_STREAM, 0);
		if (servfd < 0) {
			printf("Prefetch thread: Chiamata a socket() per serverfd fallita. Errore: %d \"%s\"\n", errno,strerror(errno));
			fflush(stdout);
			exitThread();
			return(NULL);
		}

#ifdef DEBUG
printf("Prefetch thread: %ld. Socket = %d.\n", pthread_self(), servfd);
fflush(stdout);
#endif

		/* Server socket Timeout */
		struct timeval timerserv;
		timerserv.tv_sec = INACTIVITY_TIMEOUT_SECONDS;
		timerserv.tv_usec = 0;

		if ((SetsockoptReuseAddr(servfd) == 0) ||
				(setsockopt(servfd, SOL_SOCKET, SO_RCVTIMEO, &timerserv, sizeof(struct timeval)) != 0) ||
				(setsockopt(servfd, SOL_SOCKET, SO_SNDTIMEO, &timerserv, sizeof(struct timeval)) != 0)) {
			printf("Prefetch thread: Error on a setsockopt for server socket\n");
			fflush(stdout);
			close(servfd);
			exitThread();
			return(NULL);
		}

		/* Binding */
		memset(&Local, 0, sizeof(Local));
		Local.sin_family = AF_INET;
		Local.sin_addr.s_addr = INADDR_ANY;
		Local.sin_port = htons(0);

		ris = bind(servfd, (struct sockaddr *)&Local, sizeof(Local));
		if (ris < 0) {
			printf("Prefetch thread: Chiamata a bind() per serverfd fallita. Errore: %d \"%s\"\n", errno,strerror(errno));
			fflush(stdout);
			close(servfd);
			exitThread();
			return(NULL);
		}

		/* Compilo campi server */
		memset(&Serv, 0, sizeof(Serv));
		Serv.sin_family = AF_INET;
		Serv.sin_addr.s_addr = inaddr.s_addr;
		Serv.sin_port = htons(port);

#ifdef DEBUG
printf ("Prefetch thread: %ld, %d° tentativo connessione con server. Socket: %d\n", pthread_self(), (SERVER_CHANCE-servconn_chance+1), servfd);
fflush(stdout);
#endif

		int repeat;
		repeat = 0;	
		while (1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While connect get_and_cache.\n", pthread_self());
fflush(stdout);
#endif

			ris = connect (servfd, (struct sockaddr *)&Serv, sizeof(Serv));
			if (ris < 0) {
				if ((errno == EINTR) || (errno == EINPROGRESS)) ; /* Ritento */
				if (errno == EISCONN) {
					repeat = 0;
					break;
				}
				else {
					printf("Prefetch thread: Chiamata a connect() per serverfd fallita. Errore: %d \"%s\"\n", errno,strerror(errno));
					fflush(stdout);
					if (servconn_chance > 0) servconn_chance--; /* Ritento */
					else {
						close(servfd);
						exitThread();
						return(NULL);
					}
					/* Ritento nuovo socket */
					repeat = 1;
					break;
				}	/* Errore non recuperabile */
			} /* Error on connect */
			else {
				/* OK */
				repeat = 0;
				break;
			}
		}	/* Connect while */

		/* Se è il caso torno indietro */
		if (repeat) continue;

		/* Invio la richiesta GET per file completo */
		char request[MAXLENREQ];
		sprintf(request,"GET %s\n\n",URL);
		ris = mysend(servfd, request, strlen(request));
		if (ris < 0) {
			if (servconn_chance > 0) {
				servconn_chance--;
				close(servfd);
				continue;	/* Chiudo questa connessione e ritento */
			}
			else {
				close(servfd);
				exitThread();
				return(NULL);
			}
		}

		/* Aspetto la risposta del server (presupposto myread: leggo finchè trovo 0 in lettura: server chiude) */
		ris = myread(servfd, buffer, MAXLENRESP);
		if (ris <= 0) {
			if (servconn_chance > 0) {
				servconn_chance--;
				close(servfd);
				continue;	/* Chiudo questa connessione e ritento */
			}
			else {
				close(servfd);
				exitThread();
				return(NULL);
			}
		}

		/* Concludo chiusura con server */
		close(servfd);

		/* Terminatore */
		buffer[ris] = 0;
		buffer_len = ris;

		/* Ora ho nel buffer ciò che devo salvare in file (header compresi, non da salvare) */

		int expire;
		char cachename[MAXLENPATH];
		struct timeval tod;

		/* Prelevo nome univoco */
		my_tempname(cachename);

		/* Salvo file appena scaricato in cache e prelevo expire */
		expire = buffer_data_to_file (cachename, buffer, buffer_len);

		/* Non completo / Corrotto */
		if (expire < 0) {

#ifdef DEBUG
printf("Thread: %ld. Il file per prefetching arrivato è corrotto.\n", pthread_self());
fflush(stdout);
#endif

			exitThread();
		}

		else {
			/* Prendo tempo attuale da sommare al tempo di expire del file appena ricevuto */
			gettimeofday(&tod, NULL);

			/* Gestione cache, aggiunta file */
			add_file_cache(&(inaddr), port, path, (tod.tv_sec + expire), cachename);

			/* Esco */
			break;
		}
	}

	/* Se il thread attuale ha lavorato su 1° e 2° livello ora lavoro sul 3° (REF) */
	if (prefetch_level == FIRST_SECOND_LEVEL_PREFETCHING) {
		/* Inizializzo qui, ma viene modificato nel parsing per scorrere il buffer con la risposta */
		int lenparsed = 0;
		struct Request *p_prefetch_req;
		char tmpname[MAXLENPATH];

		while(1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While2 get_cache_name.\n", pthread_self());
fflush(stdout);
#endif

			/* Struttura Request per fare richiesta al server, già passabile come parametro a thread */
			p_prefetch_req = (struct Request *) malloc (sizeof(struct Request));

			if (p_prefetch_req == NULL){

				exitThread();
				return(NULL);
			}

			/* 
				Chiamo il parsing saltando almeno il minimo numero di byte che possono esserci prima dei dati.
				NB: 
				- Uso campo "buf[]" della struttura Request per l'URL!
				- Chiedo a parse() solo URL da REF, per il 3° livello
			*/

			ris = parse((buffer+MIN_BYTE_BEFORE_DATA), &lenparsed, (buffer_len-MIN_BYTE_BEFORE_DATA), p_prefetch_req, THIRD_LEVEL_PREFETCHING);

			/* Parsing finito, per errori o fine buffer */
			if(ris <= 0) {
				exitThread();
				return(NULL);
			}

			else if(ris == 1) {

				/* Thread che faranno prefetch esclusivamente su REF */
				pthread_attr_t attr;
				pthread_attr_init(&attr);
				pthread_t threadID;

				pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

				/* Se ho già il file passo al prossimo URL */
				if(get_cache_name(&(p_prefetch_req->inaddr), p_prefetch_req->port, p_prefetch_req->path, tmpname)) {
					free(p_prefetch_req);
					p_prefetch_req = NULL;
					continue;
				}

				/* Altrimenti setto il campo reqType della struttura Request che uso come parametro al thread, impostandolo per 
					il prefetching di terzo livello.. */
				p_prefetch_req->reqType = THIRD_LEVEL_PREFETCHING;

				/* ..e faccio partire il thread relativo alla sessione di prefetching per URL specificato in p_prefetch_req->buf[] */
				ris = pthread_create(&threadID, &attr, get_and_cache, (void *)p_prefetch_req);
				if(ris != 0) {

#ifdef DEBUG
printf("Thread: %ld. Chiamata a pthread_create() fallita.\n Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
fflush(stdout);
#endif
					continue;
				}

			}

		} /* while del parsing */
	} /* Fine prefetch 2° livello */

	/* Altrimenti se il thread attuale ha lavorato sui REF per il 3° livello ho finito */
	exitThread();
	return(NULL);

}
