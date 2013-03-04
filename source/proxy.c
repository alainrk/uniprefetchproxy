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
	proxy.c

	Il modulo contiene il main del proxy e la definizione della funzione per i thread
	principali dello stesso.
	Essenzialmente il proxy cicla continuamente facendo partire	per ogni connessione 
	stabilita con un client dopo l'accept, un thread "session" che gestirà l'arrivo 
	della richiesta, l'invio della stessa al server, la ricezione (con gestione degli 
	errori) della risposta, il forward al client, e contemporaneamente genererà i 
	thread per il prefetching di 1°/2° livello; a loro volta questi faranno partire
	altri thread (solo per sequenze REF) per il prefetching di 3° livello sui contenuti
	appena ricevuti.
	Per la gestione interna dei file in cache, vi sono delle strutture dati gestite dal
	modulo caching.c, mentre il thread principale per il prefetching è situato nel 
	modulo prefetching.c, infine il modulo util.c contiene tutte le funzioni ausiliarie
	al corpo principale del proxy, quali quelle per il parsing, le connessioni, la 
	lettura e scrittura con tolleranza a ritardi/errori e così via.
	Ulteriori moduli sono quelli per extern, costanti e strutture.
	La cartella "cache" contiene tutti i file	per il prefetching durante l'esecuzione
	e viene svuotata ad ogni partenza del proxy.

	La lista per la "contabilità" dei file in cache viene ogni tanto ripulita dei
	file expired dal thread daemon_cache_clean.

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
#include <dirent.h> /* Per scorrere nella directory cache e cancellare i files */

#include "caching.e"
#include "prefetching.e"
#include "util.e"

int numthread;
pthread_mutex_t mutex_num;
pthread_mutex_t mutex_name;
pthread_mutex_t mutex_cache;


#ifdef DAEMON_CACHE_CLEAN

/* 
	Thread "demone" di pulizia per la lista di file in cache.
	Ogni tanto si attiva per ripulire la lista di tutte le 
	strutture, e i file associati che sono expired.
*/
#define DAEMON_PAUSE 30
static void *daemon_cache_clean () {

	struct file_cache *curr;	/* Punta alla struttura corrente */
	struct file_cache* *prev;	/* Punta al puntatore a struttura precedente */
	char temp_name[MAXLENPATH]="../cache/";
	struct timeval tod;

	while(1) {

		sleep (DAEMON_PAUSE);

		/* Prendo il tempo, potrei farlo nel ciclo per più precisione ma a costo di prestazioni */
		if ((gettimeofday(&tod, NULL)) < 0) 
			return 0;

		/******************* MUTUA ESCLUSIONE ********************/
		#ifdef MUTEX_CACHE
		pthread_mutex_lock(&mutex_cache);
		#endif

		curr = sentinel;
		prev = &sentinel;

		/* Se la lista è finita, sblocco la mutex (se attiva -> vedi define) e torno a sleep */
		while (curr != NULL) {

			/* Se il file è scaduto lo cancello e lo tolgo dalla lista */
			if (tod.tv_sec > curr->expire) {

#ifdef DEBUG
printf("Controllo su While daemon_cache_clean -> Cancello file expired.\n");
fflush(stdout);
#endif

				/* Unisco path cache con nome nella cartella */
				strncat(temp_name, curr->cache_name, strlen(temp_name)+strlen(curr->cache_name));
				remove(temp_name);
				/* Faccio puntare il precedente (anche se fosse sentinel) al successivo, scavalcandomi */
				(*prev) = curr->next;
				free(curr);
			}

			else {
#ifdef DEBUG
printf("Controllo su While daemon_cache_clean -> File NON expired.\n");
fflush(stdout);
#endif
			}

			/* Avanzo alla prossima struttura */
			curr = curr->next;
			prev = &((*prev)->next);
		}

		#ifdef MUTEX_CACHE
		pthread_mutex_unlock(&mutex_cache);
		#endif
		/******************** MUTUA ESCLUSIONE ********************/
	}
}
#endif


/* Thread per sessione di connessione */
static void *Session (void *p) {

	int clifd;												/* Socket Client-Proxy */
	int servfd;												/* Socket Proxy-Server */
	int ris;													/* Utili */
	struct Request req;								/* Conterrà le informazioni per forward e prefetch */
	struct Response resp;							/* Conterrà info per invio file cache, struttura richiesta da prepareResponse ecc */
	char cache_filename[MAXLENPATH];
	char error_response[16];
	char buffer[MAXLENRESP];
	int buffer_len;
	struct sockaddr_in Local, Serv; 	/* Per connessione con il server */

	/* Client socket Timeout */
	struct timeval timercli;
	timercli.tv_sec = INACTIVITY_TIMEOUT_SECONDS;
	timercli.tv_usec = 0;

	/* Estrazione informazioni parametro thread */
	clifd = ((struct param*)p)->fd;
	free(p);
	p = NULL;

	/* Controllo sul numero dei thread */
	pthread_mutex_lock(&mutex_num);
	if(numthread < MAXNUMTHREADWORKING)
		numthread++;
	pthread_mutex_unlock(&mutex_num);
	if(numthread >= MAXNUMTHREADWORKING) {
		close(clifd);
		exitThread();
		return(NULL);
	}

#ifdef DEBUG
printf("Thread: %ld. Client FD passato: %d\n", pthread_self(), clifd);
fflush(stdout);
#endif

	/* Imposto il timeout per questo socket client */
	setsockopt(clifd, SOL_SOCKET, SO_RCVTIMEO, &timercli, sizeof(struct timeval));
	setsockopt(clifd, SOL_SOCKET, SO_SNDTIMEO, &timercli, sizeof(struct timeval));

	/* Inizializzo struttura per richiesta */
	initRequest(&req);

	/* 
		Prelevo la richiesta ed estraggo le informazioni.
		Se a buon fine l'avrò tutta in req->buf, mentre il resto sarà estratto e messo negli altri campi di req.
	*/
	ris = getRequest(clifd,&req);

	switch(ris) {

		case -3: /* Client ha chiuso la connessione */

#ifdef DEBUG
printf("Thread: %ld. Client ha chiuso connessione. \nBuffer: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

			close(clifd);
			exitThread();
			return(NULL);
		break;

		case -2: /* Errore in lettura, chiudo thread e connessione */

#ifdef DEBUG
printf("Thread: %ld. Errore in lettura client request. \nBuffer: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

			close(clifd);
			exitThread();
			return(NULL);
		break;

		case -1: /* Lettura richiesta terminata, formato richiesta sbagliato */
			strcpy(error_response, "403\n\n");
			mysend(clifd, error_response, strlen(error_response));

#ifdef DEBUG
printf("Thread: %ld. Lettura req terminata, formato richiesta sbagliato. \nBuffer: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

			close(clifd);
			exitThread();
			return(NULL);
		break;

		case 1: /* Lettura richiesta OK -> proseguo */

#ifdef DEBUG
printf("Thread: %ld. Lettura req OK. \nBuffer: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

		break;
		
		default: /* Errore sconosciuto */
			strcpy(error_response, "402\n\n");
			mysend(clifd, error_response, strlen(error_response));

#ifdef DEBUG
printf("Thread: %ld. Errore sconosciuto. \nBuffer: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

			close(clifd);
			exitThread();
			return(NULL);
		break;

	}	/* Switch */

	/* Ora ho nella struttura Request tutte le informazioni che mi servono */

	/* Tentativi connessione server */
	int servconn_chance = SERVER_CHANCE;

	/* 
		Ogni qualvolta ritento una connessione con il server per errori, 
		ricontrollo di avere il file in cache, qualche altro thread 
		potrebbe averlo	salvato nel frattempo.
	*/
	while (1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While session().\n", pthread_self());
fflush(stdout);
#endif

		/* Se ha fatto una richiesta GET e ho il file in cache */
		if ((req.reqType == GET) && (get_cache_name(&(req.inaddr), req.port, req.path, cache_filename))) {

			/* Cerco di preparare la risposta da mandare (Anche con RANGE), l'eventuale contenuto del file è nel buffer di resp */
			ris = prepareGetResponse(&req, &resp, cache_filename);

#ifdef DEBUG
printf("Thread: %ld. La req è GET e ho il file in cache. \nFile: %s\n", pthread_self(), resp.buf);
fflush(stdout);
#endif

			switch(ris) {

				case 1:
					ris = mysend(clifd, resp.buf, resp.lenresp);	/* caso OK ------> MANDO AL CLIENT IL FILE CHE ERA IN CACHE e chiudo */

					if (ris == -1) {
						printf ("Error on cached file response send.\n");
						fflush(stdout);
					}
					if (ris == -2) {
						printf ("Time out on cached file response send.\n");
						fflush(stdout);
					}
					if (ris == 0) {
						printf ("Client closed connection before sending cached file.\n");
						fflush(stdout);
					}
					else {
						printf ("Cached file sent to client.\n");
						fflush(stdout);
					}
					close(clifd);
					exitThread();
					return(NULL);
				break;

				case -3: /* file not found (expired nel frattempo) ---> faccio la richiesta */

#ifdef DEBUG
printf("Thread: %ld. Avevo il file ma è expired. \n", pthread_self());
fflush(stdout);
#endif

				break;

				case -4: /* interval not found -----> lo notifico */
					strcpy(error_response, "405\n\n");
					mysend(clifd, error_response, strlen(error_response));

#ifdef DEBUG
printf("Thread: %ld. Avevo il file ma il RANGE è sbagliato. \n", pthread_self());
fflush(stdout);
#endif

					close(clifd);
					exitThread();
					return(NULL);
				break;

				case -5: /* unknown error */
				default:
					strcpy(error_response, "402\n\n");
					mysend(clifd, error_response, strlen(error_response));

#ifdef DEBUG
printf("Thread: %ld. Avevo il file ma dopo la prepareResponse errore sconosciuto.\n", pthread_self());
fflush(stdout);
#endif

					close(clifd);
					exitThread();
					return(NULL);
				break;
			}
		} /* Fine GET e ho il file in cache */
	
	/* Non ho file in cache oppure richiesta INF -> Forward richiesta al server e poi discrimino in base a GET (prefetch) e INF (solo forward risposta) */

		servfd = socket(AF_INET, SOCK_STREAM, 0);
		if (servfd < 0) {
			printf("Thread: %ld. Chiamata a socket() per serverfd fallita. Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
			fflush(stdout);
			close(clifd);
			exitThread();
			return(NULL);
		}

		/* Server socket Timeout */
		struct timeval timerserv;
		timerserv.tv_sec = INACTIVITY_TIMEOUT_SECONDS;
		timerserv.tv_usec = 0;

		if ((SetsockoptReuseAddr(servfd) == 0) ||
				(setsockopt(servfd, SOL_SOCKET, SO_RCVTIMEO, &timerserv, sizeof(struct timeval)) != 0) ||
				(setsockopt(servfd, SOL_SOCKET, SO_SNDTIMEO, &timerserv, sizeof(struct timeval)) != 0)) {
			printf("Thread: %ld. Error on a setsockopt for server socket\n", pthread_self());
			fflush(stdout);

			close(servfd);
			close(clifd);
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
			printf("Thread: %ld. Chiamata a bind() per serverfd fallita. Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
			fflush(stdout);
			close(servfd);
			exitThread();
			return(NULL);
		}

		/* Compilo campi server */
		memset(&Serv, 0, sizeof(Serv));
		Serv.sin_family = AF_INET;
		Serv.sin_addr.s_addr = req.inaddr.s_addr;
		Serv.sin_port = htons(req.port);

#ifdef DEBUG
printf ("Thread: %ld, %d° tentativo connessione con server. FD: %d\n", pthread_self(), (SERVER_CHANCE-servconn_chance+1), servfd);
fflush(stdout);
#endif

		int repeat;
		repeat = 0;	
		while (1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While connect session.\n", pthread_self());
fflush(stdout);
#endif

			ris = connect (servfd, (struct sockaddr *)&Serv, sizeof(Serv));
			if (ris < 0) {
				if ((errno == EINTR) || (errno == EINPROGRESS)) ; /* Ritento */
				if (errno == EISCONN) {
					printf("Thread: %ld, Chiamata a connect() per serverfd fallita. FD: %d. Errore: %d \"%s\"\n", pthread_self(), servfd, errno,strerror(errno));
					fflush(stdout);
					repeat = 0;
					break;
				}
				else {
					printf("Thread: %ld, Chiamata a connect() per serverfd fallita. FD: %d. Errore: %d \"%s\"\n", pthread_self(), servfd, errno,strerror(errno));
					fflush(stdout);
					if (servconn_chance > 0) servconn_chance--; /* Ritento */
					else {
						printf("Thread: %ld, Chiamata a connect() per serverfd fallita. FD: %d. Errore: %d \"%s\"\n", pthread_self(), servfd, errno,strerror(errno));
						fflush(stdout);
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

		/* Forwardo la richiesta, qualunque essa sia (inf/get) */

#ifdef DEBUG
printf("Thread: %ld. Forward richiesta del client al server.\n Req: %s\n", pthread_self(), req.buf);
fflush(stdout);
#endif

		ris = mysend(servfd, req.buf, strlen(req.buf));
		if (ris < 0) {

#ifdef DEBUG
printf("Thread: %ld. Errore su forward req.\n", pthread_self());
fflush(stdout);
#endif

			if (servconn_chance > 0) {
				servconn_chance--;
				close(servfd);
				continue;	/* Chiudo questa connessione e ritento */
			}
			else {
				close(clifd);
				close(servfd);
				exitThread();
				return(NULL);
			}
		}

		/* Aspetto la risposta del server */
		ris = myread(servfd, buffer, MAXLENRESP);
		if (ris <= 0) {

#ifdef DEBUG
printf("Thread: %ld. Errore in risposta server a forward req del client.\nErrore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
fflush(stdout);
#endif

			if (servconn_chance > 0) {
				servconn_chance--;
				close(servfd);
				continue;	/* Chiudo questa connessione e ritento */
			}
			else {
				close(clifd);
				close(servfd);
				exitThread();
				return(NULL);
			}
		}

#ifdef DEBUG
printf("Thread: %ld. OK arrivo risposta dal server per forward al client.\n Resp: %s\n", pthread_self(), buffer);
fflush(stdout);
#endif

		/* Concludo chiusura con server */
		close(servfd);

		/* Terminatore */
		buffer[ris] = '\0';
		buffer_len = ris;

		/* Controllo errori 40X nel qual caso forwardo e chiudo */
		if ((!(strncmp(buffer,"402\n\n",5))) ||
				(!(strncmp(buffer,"403\n\n",5))) ||
				(!(strncmp(buffer,"404\n\n",5))) ||
				(!(strncmp(buffer,"405\n\n",5)))) {

#ifdef DEBUG
printf("Thread: %ld. Server ha inviato un errore 40X, faccio forward al Client", pthread_self());
fflush(stdout);
#endif

			ris = mysend(clifd, buffer, buffer_len);
			close(clifd);
			exitThread();
			return(NULL);
		}

		/* Se la richiesta era INF faccio un parsing per INF e invio a client */
		if (req.reqType == INF) {
			/* Corretto */
			if (parseINF(buffer, buffer_len) == 0){
				ris = mysend(clifd, buffer, buffer_len);
				if (ris < 0) /* Errore irreparabile col client */

#ifdef DEBUG
printf("Thread: %ld. Errore in risposta a Client (dopo arrivo dal server).\n Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
fflush(stdout);
#endif

				close(clifd);
				exitThread();
				return(NULL);
			}
			/* Errato */
			else
				continue; /* Ritento */
		}

		/* Se la richiesta era GET per RANGE faccio il parsing per RANGE e poi invio il file */
		if (req.is_range) {
			/* Corretto */
			if (parseRANGE(buffer, buffer_len) == 0) ; /* Vado comunque a fare il parsing per fare prefetch sugli URL che trovo */
			/* Errato */
			else
				continue; /* Ritento */
		}

		int expire;
		char cachename[MAXLENPATH];
		struct timeval tod;

		/* Se invece la richiesta era GET per file completo prima faccio parsing per GET completo.
			 Se corretto salvo il file (così è a disposizione di altri thread) e poi invio a client */
		if (!(req.is_range)) {

#ifdef DEBUG
printf("Thread: %ld. Prelevo nome per caching.\n", pthread_self());
fflush(stdout);
#endif

			/* Prelevo nome univoco */
			my_tempname(cachename);

#ifdef DEBUG
printf("Thread: %ld. Salvo file caching.\n", pthread_self());
fflush(stdout);
#endif


			/* SE CORRETTO E COMPLETO salvo file appena scaricato in cache e prelevo expire */
			expire = buffer_data_to_file (cachename, buffer, buffer_len);

			/* Non completo / Corrotto */
			if (expire < 0) {

#ifdef DEBUG
printf("Thread: %ld. Il file arrivato è corrotto.\nEcco il file:\n", pthread_self());
fflush(stdout);
#endif

				write(1, buffer, MAXLENRESP);
				fflush(stdout);
				continue; /* Ritento da capo connessione con server e richiesta */
			}

			else {
				/* Prendo tempo attuale da sommare al tempo di expire del file appena ricevuto */
				gettimeofday(&tod, NULL);

				/* Gestione cache, aggiunta file */
				add_file_cache(&(req.inaddr), req.port, req.path, (tod.tv_sec + expire), cachename);
			}
		}
		

		/* Ora ho nel buffer ciò che devo inviare a Client e in ogni caso faccio lo stesso tipo di invio */
		ris = mysend(clifd, buffer, buffer_len);
		if (ris < 0) {	/* Errore irreparabile col client */

#ifdef DEBUG
printf("Thread: %ld. Errore in risposta a Client (dopo arrivo dal server).\n Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
fflush(stdout);
#endif

			close(clifd);
			exitThread();
			return(NULL);
		}

#ifdef DEBUG
printf("Thread: %ld. OK risposta dopo arrivo file del Server verso Client.\n Resp: %s\n", pthread_self(), buffer);
fflush(stdout);
#endif

		/* In ogni caso ho finito col client */
		close(clifd);

		/* Inizializzo qui, ma viene modificato nel parsing per scorrere il buffer con la risposta */
		int lenparsed = 0;
		struct Request *p_prefetch_req;
		char tmpname[MAXLENPATH];

#ifdef DEBUG
printf("Thread: %ld. Fase prefetching.\n", pthread_self());
fflush(stdout);
#endif

		while(1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While parse session().\n", pthread_self());
fflush(stdout);
#endif

			/* Struttura Request per fare richiesta al server, già passabile come parametro a thread */
			p_prefetch_req = (struct Request *) malloc (sizeof(struct Request));

			if (p_prefetch_req == NULL){

#ifdef DEBUG
printf("Thread: %ld. Errore in allocazione p_prefetch_req.\n", pthread_self());
fflush(stdout);
#endif

				exitThread();
				return(NULL);
			}
			/* Chiamo il parsing saltando almeno il minimo numero di byte che possono esserci prima dei dati.
				 NB: USO CAMPO "buf[]" della struttura Request per l'URL!! 
				 NB2: Chiedo sia REF che IDX;REF per 1° e 2° livello
			*/

#ifdef DEBUG
printf("Thread: %ld. Fase parsing del buffer.\n", pthread_self());
fflush(stdout);
#endif

			ris = parse((buffer+MIN_BYTE_BEFORE_DATA), &lenparsed, (buffer_len-MIN_BYTE_BEFORE_DATA), p_prefetch_req, FIRST_SECOND_LEVEL_PREFETCHING);

			/* Parsing finito, per errori o fine buffer */
			if(ris <= 0) {
				exitThread();
				return(NULL);
			}

			else if(ris == 1) {

#ifdef DEBUG
printf("Thread: %ld. OK parsing, URL estratto: %s.\n", pthread_self(), p_prefetch_req->buf);
fflush(stdout);
#endif

				/* Thread che fanno prefetch su REF - IDX+REF */
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
					il prefetching di primo e secondo livello.. */
				p_prefetch_req->reqType = FIRST_SECOND_LEVEL_PREFETCHING;

				/* .. e faccio partire il thread relativo alla sessione di prefetching per URL specificato in p_prefetch_req->buf[] */
				ris = pthread_create(&threadID, &attr, get_and_cache, (void *)p_prefetch_req);
				if(ris != 0) {
					printf("Thread: %ld. Chiamata a pthread_create() fallita.\n Errore: %d \"%s\"\n", pthread_self(), errno,strerror(errno));
					fflush(stdout);
					continue;
				}

			}

		} /* while del parsing */

	} /* While socket/binding */

} /* Session ****************/


void handler (int signo) {
	printf("Termino con signal: %d\n", signo);
	fflush(stdout);
	exit(0);
}


int main(int argc, char *argv[]) {

	int listenfd;
	struct sockaddr_in Client, Local;
	unsigned int len;
	int ris;
	int local_port;
	pthread_attr_t attr;
	
	/* Aggancio segnali */
	if (signal(SIGINT,handler) == SIG_ERR) {
		printf("Signal fallita\n");
		fflush(stdout);
		exit(1);
	}


	if (argc == 1) {
		printf ("Porta di default: 55554\n\n");
		fflush(stdout);
		local_port = 55554;
	}
	
	else if (argc == 2)
			local_port = atoi(argv[1]);

	/* Controllo argomenti */
	else {
		printf ("Usage: ./proxy local_port\n");
		fflush(stdout);
		return(1);
	}

	printf("Cleaning cache...\n\n");
	fflush(stdout);

	/* Cancellazione file della cache */
	DIR *dir_p;
	struct dirent *entry;
	char dir_cache[]="../cache/";
	char toremove[512];
	/* Creo nome file e cancello */
	dir_p = opendir(dir_cache);

	while (1) {

#ifdef DEBUG
printf("While cleaning cache\n");
fflush(stdout);
#endif

		if ((entry = readdir(dir_p)) == NULL) break;
		strcpy(toremove, dir_cache);
		/* 
			Evito di cancellare file nascosti e le directory speciali unix "." e "..",
			operazione comunque non possibile con uid del proxy
		*/
		if ((strncmp(entry->d_name, ".", sizeof("."))) && 
				(strncmp(entry->d_name, "..", sizeof("..")))) {
			remove(strcat(toremove, entry->d_name));
			printf("Removing \"%s\"\n", entry->d_name);
			fflush(stdout);
		}
	}

	printf("\nCache clean.\n");
	fflush(stdout);

	/* Inizializzazione strutture cache */
	init_cache_list();

	/* Inizializzazione thread */
	pthread_mutex_init(&mutex_num,NULL);
	pthread_mutex_init(&mutex_name,NULL);
	pthread_attr_init(&attr);

	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

#ifdef DAEMON_CACHE_CLEAN
	pthread_t threadID_daemon;

	/* Faccio partire il thread "demone" per la pulizia cache */
	ris = pthread_create(&threadID_daemon, &attr, daemon_cache_clean, NULL);

	if(ris != 0) {
		printf("Chiamata a pthread_create() fallita.\n Errore: %d \"%s\"\n", errno,strerror(errno));
		fflush(stdout);
		exit(1);
	}
#endif

	int repeat;

	while (1) {

#ifdef DEBUG
printf("While nel main, per listen\n");
fflush(stdout);
#endif

		listenfd = 0;

		listenfd = socket(AF_INET, SOCK_STREAM, 0);
		if (listenfd < 0)	{	
			printf ("socket() failed, Err: %d \"%s\"\n", errno,strerror(errno));
			fflush(stdout);
			exit(1);
		}

		memset ( &Local, 0, sizeof(Local) );
		Local.sin_family= AF_INET;
		Local.sin_addr.s_addr = htonl(INADDR_ANY);
		Local.sin_port	      = htons(local_port);

		/* Evito EADDRINUSE sulla bind() */
		ris = SetsockoptReuseAddr(listenfd);
		if (ris == 0)	{
			close(listenfd);
			exit(1);
		}

		ris = bind(listenfd, (struct sockaddr*) &Local, sizeof(Local));
		if (ris < 0) {	
			printf ("bind() failed, Err: %d \"%s\"\n",errno,strerror(errno));
			fflush(stdout);
			exit(1);
		}

		ris = listen(listenfd, 100);
		if (ris < 0)	{	
			printf ("listen() failed, Err: %d \"%s\"\n",errno,strerror(errno));
			fflush(stdout);
			exit(1);
		}

		/* Abilito/Riabilito entrata in ciclo per accept */
		repeat = 1;

		/* Ciclo infinito in attesa di richieste al proxy */
		while(repeat) {

#ifdef DEBUG
printf("While nel main, per accept\n");
fflush(stdout);
#endif

			memset ( &Client, 0, sizeof(Client) );
			len = sizeof(Client);

#ifdef DEBUG
printf("Aspetto clients");
fflush(stdout);
#endif

			/* Attendi richiesta di connessione */
			ris = accept(listenfd,(struct sockaddr *)&Client, &len);

#ifdef DEBUG
printf("Dopo accept");
fflush(stdout);
#endif

			if(ris < 0){
				if(errno == EINTR) 
					continue;	/* Ritenta */
				else {
					printf("Chiamata ad accept() fallita.\n Errore: %d \"%s\"\n", errno,strerror(errno));
					fflush(stdout);
					close(listenfd);
					repeat = 0; /* Altro socket listening */
					/*exit(1);*/
				}
			}

			/* 
				Accept a buon fine, nuova connessione con client.
			 	Faccio partire sessione cli-pro-[serv-pro-]cli.
			*/
			else {
			
				/* Struttura per passaggio parametri al thread */
				struct param *p;

				p = malloc(sizeof(struct param));
				if(p == NULL) {
					printf("Chiamata a malloc() fallita.\n Errore: %d \"%s\"\n", errno,strerror(errno));
					fflush(stdout);
					exit(1);
				}
				else {
					pthread_t threadID;

					/* Passo al thread il socket che identifica la connessione con questo client */
					p->fd = ris;
				
					/* Parte il thread relativo alla sessione con questo client */
					ris = pthread_create(&threadID, &attr, Session, (void*)p);

					if(ris != 0) {
						printf("Chiamata a pthread_create() fallita.\n Errore: %d \"%s\"\n", errno,strerror(errno));
						fflush(stdout);
						exit(1);
					}
					p = NULL;
				}

			} /* Creazione Thread */
		} /* While accept */
	} /* While listen */
} /* Main */
