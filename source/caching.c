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
	caching.c 

	Questo modulo gestisce le strutture per la memorizzazione
	dei file di caching.
	Implementa la struttura allegata al file.
	Implementa le funzioni utili alla gestione per lista di tale strutture.

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
#include <sys/stat.h>	/* open() */


#include "proxy.e"
#include "util.e"



/* Sentinella della lista */
struct file_cache *sentinel;

/* Per generare stringhe da numeri */
int global_cache_name;

/* Inverte una stringa */
void inverti_stringa(char *string) {
	int curr;
	int i, k;
	
	k = strlen(string) - 1;

	for (i = 0; i < k; i++, k--) {
		curr = string[i];
		string[i] = string[k];
		string[k] = curr;
	}
}

/* 
	Prende il valore intero della variabile globale,
	lo trasforma in stringa, così da avere un nome univoco
	per il file che si creerà in cache.
*/
void get_seq_name(char *string) {	
	int i;

	/* Ottieni numero e incrementa il globale */
	int num = global_cache_name++;

	i = 0;
	/* Per comodità genera al contrario e poi inverto */
	do {

#ifdef DEBUG
printf("Controllo su thread: %ld. Do-While get_seq_name().\n", pthread_self());
fflush(stdout);
#endif

		/* Una cifra per volta */
		string[i++] = num % 10 + '0';
	} while ((num /= 10) > 0);   /* elimina la cifra da n */

	string[i] = '\0';
	inverti_stringa(string);
}


/* Inizializzo la lista, inizialmente vuota */
void init_cache_list() {
	sentinel = NULL;
	/* Inizializzo intero per generare nomi */
	global_cache_name = 0;

}

/* Restituisce un nome univoco per un file */
void my_tempname(char *buffer){

	/* Genero un nome da dare al file e verrà già messo nel buffer per la restituzione -> in mutua esclusione */
	pthread_mutex_lock(&mutex_name);

  get_seq_name(buffer);

	pthread_mutex_unlock(&mutex_name);

}

/* 
	Prende le informazioni sul file:
		- Addr server (come struttura in_addr)
		- Porta server
		- Path sul server
		- Expire, espresso come data in secondi -> sarà passato come "gettimeofday_attuale + expire_indicato_nel_response"
		- Nome del file associato

	NON CREA IL FILE!!
*/
void add_file_cache(struct in_addr *inaddr_target, uint16_t port_target, const char *path_target, int expire_target, char *filename) {
	struct file_cache *new;

	/* Alloco spazio per la nuova struttura */
	new = (struct file_cache *) malloc (sizeof(struct file_cache));
	if (new == NULL) {

#ifdef DEBUG
printf("Caching per thread: %ld. Malloc add_file_cache fallita.\n", pthread_self());
fflush(stdout);
#endif

		exitThread();
	}

	/* memset(new, 0, sizeof(struct file_cache)); */

	/* Copio i valori nei rispettivi campi */
	sprintf(new->URL, "mhttp://%s:%i/%s",inet_ntoa(*inaddr_target), port_target, path_target);
	new->expire = expire_target;
	strncpy(new->cache_name, filename, strlen(filename));

	/* Aggiungo la struttura in testa alla lista */
	struct file_cache *tmp;

	pthread_mutex_lock(&mutex_cache);

  tmp = sentinel;
	sentinel = new;
	new->next = tmp;

	pthread_mutex_unlock(&mutex_cache);
}



/* Dato un path (sul server), addr e porta, restituisce in buffer il corrispondente nome del file sul proxy (0 se non c'è) */
int get_cache_name(struct in_addr *inaddr_target, uint16_t port_target, const char *path_target, char *buffer) {
	struct file_cache *curr;	/* Punta alla struttura corrente */
	struct file_cache* *prev;	/* Punta al puntatore a struttura precedente */
	struct timeval tod;
	char temp_name[MAXLENPATH]="../cache/";
	char temp_URL[MAXLENREQ];

	/* Creo URL da confrontare con gli altri */
	sprintf(temp_URL, "mhttp://%s:%i/%s",inet_ntoa(*inaddr_target), port_target, path_target);

	/* Prendo il tempo, potrei farlo nel ciclo per più precisione ma a costo di prestazioni */
	if ((gettimeofday(&tod, NULL)) < 0) 
		return 0;

	/******************* MUTUA ESCLUSIONE ********************/
	#ifdef MUTEX_CACHE
	pthread_mutex_lock(&mutex_cache);
	#endif

	curr = sentinel;
	prev = &sentinel;
	
	/* Faccio una ricerca a discesa sulle caratteristiche del file richiesto, se tutto corrisponde torna 1, altrimenti 0 */

	while (curr != NULL) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While get_cache_name.\n", pthread_self());
fflush(stdout);
#endif

		/* Se l'URL corrisponde */
		if (!(strncmp(temp_URL, curr->URL, MAXLENREQ))) {
			/* Se il file non è scaduto */
			if (tod.tv_sec < curr->expire) {
				/* Metto nel buffer il nome corrispondente e ritorno 1 */
				strncpy(buffer, curr->cache_name, sizeof(curr->cache_name));

#ifdef DEBUG
printf("HO IL FILE\n");
fflush(stdout);
#endif

				#ifdef MUTEX_CACHE
				pthread_mutex_unlock(&mutex_cache);
				#endif
				return 1;
			}
			/* Altrimenti cancello il file scaduto e lo tolgo dalla lista */
			else {
				/* Unisco path cache con nome nella cartella */
				strncat(temp_name, curr->cache_name, strlen(temp_name)+strlen(curr->cache_name));
				/* Elimino il file -> Se qualcuno lo sta leggendo verrà cancellato appena chiuso	
					NB: In ogni caso quel file al modulo di caching è estraneo in quanto non presente
							nella lista, non c'è quindi il pericolo di dare un file expired, fatta eccezione
							dei thread che stanno (se ce ne sono) inviando il file in questo momento
				*/
				remove(temp_name);
				/* Faccio puntare il precedente (anche se fosse sentinel) al successivo, scavalcandomi */
				(*prev) = curr->next;
				free(curr);
				curr = NULL;

#ifdef DEBUG
printf("FILE SCADUTO\n");
fflush(stdout);
#endif

				break;
			}
		}

		/* Altrimenti avanzo alla prossima struttura */
		curr = curr->next;
		prev = &((*prev)->next);
	}

	#ifdef MUTEX_CACHE
	pthread_mutex_unlock(&mutex_cache);
	#endif
	/******************** MUTUA ESCLUSIONE ********************/

	/* Il file non è in cache */
	buffer = NULL;

#ifdef DEBUG
printf("NON HO IL FILE\n");
fflush(stdout);
#endif

	return 0;
}


/* 
	Funzione che prende un nome di file, un buffer contenente una risposta 200 - Dati, e la lunghezza del buffer.
	Estrae i dati in esso contenuti per metterli in un file, che creerà con il nome ricevuto.
	Inoltre restituirà l'Expire. -1 in caso di errori e in questi casi NON SALVA il file ovviamente.
*/
int buffer_data_to_file (char *filename, char *buf, int len) {

  int data_len; 							/* Lunghezza parte dati da mettere in file */
	int expire;
	int fd;

	if(len<1) {
		printf ("Estrazione dati ed expire: non è arrivato nulla dal server.\n");
		fflush(stdout);
		return(-1);
	}

	if(len>=1) 
		if(buf[0]!='2') 
			return(-1);

	if(len>=2)
		if(buf[1]!='0')
			return(-1);
		
	if(len>=3) 
		if(buf[2]!='0')
			return(-1);
		
	if(len>=4)
		if(buf[3]!='\n')
			return(-1);

	/* cerco se c'e' Len xxxx\n  */
	if(len>=5) 
		if(buf[4]!='L')
			return(-1);
		
	if(len>=6) 
		if(buf[5]!='e')
			return(-1);
		
	if(len>=7)
		if(buf[6]!='n')
			return(-1);

	if(len>=8)
		if(buf[7]!=' ')
			return(-1);
		
	
	if(len>=9) {

		int ris, len2, len3;
		char str[1024];

		ris=sscanf(buf+8, "%i", &len2);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, len negativo */

		/* salvo la lunghezza del file scaricato */
		data_len = len2;

		sprintf(str,"%i", len2 );
		/* cerco il carattere successivo alla fine del numero trovato */
		len2= 9+strlen(str);

		if(len>=len2)
			if(buf[len2-1]!='\n')
				return(-1);
		
		/* cerco se c'e' Expire xxxx\n  */
		if(len>=len2+1)
			if(buf[len2+1-1]!='E')
				return(-1);
		
		if(len>=len2+2)
			if(buf[len2+2-1]!='x')
				return(-1);
		
		if(len>=len2+3)
			if(buf[len2+3-1]!='p')
				return(-1);
		
		if(len>=len2+4)
			if(buf[len2+4-1]!='i')
				return(-1);
		
		if(len>=len2+5)
			if(buf[len2+5-1]!='r')
				return(-1);
		
		if(len>=len2+6)
			if(buf[len2+6-1]!='e')
				return(-1);
		
		if(len>=len2+7)
			if(buf[len2+7-1]!=' ')
				return(-1);

		if(len<len2+7+1)
			return(-1);

		ris=sscanf(buf+len2+7, "%i", &len3);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len3<0 ) return(-1); /* errato, expire negativo */

		/* Salvo l'expire */
		expire = len3;

		/* me ne frego dell' expire */
		sprintf(str,"%i", len3 );
		/* cerco il carattere successivo alla fine del numero trovato */
		len2= len2 + 8 + strlen(str);

		/* controllo se c'e' l' EOL */
		if(len>=len2)
			if(buf[len2-1]!='\n')
				return(-1);
	
		/* cerco la fine del messaggio */
		if(len>=(len2+1))
			if(buf[len2+1-1]!='\n')
				return(-1);
		
		/* 
			se arrivo qui, 
		 	len2+1 e' l'indice dell'ultimo byte del messaggio mhttp
		 	ma dovrebbero esserci ancora data_len byte
		 	di cui il primo char dovrebbe avere indice len2+2
		*/

		if( len < (len2+1+data_len) )
			return(-1);

		char path[MAXLENPATH];

		/* Creo il nome del file di cache sul proxy */
		strcpy(path, "../cache/");
		strcat(path, filename);

#ifdef DEBUG
printf("Caching per thread: %ld. File che apro: %s.\n", pthread_self(), path);
fflush(stdout);
#endif

		fd = open(path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);

#ifdef DEBUG
printf("Caching per thread: %ld. FD per write = %d.\n", pthread_self(), fd);
fflush(stdout);
#endif

		if (fd < 0) {
			printf("Chiamata a open() per cache file fallita. Errore: %d \"%s\"\n", errno,strerror(errno));
			fflush(stdout);
			exitThread();
			remove(path);
			return(-1);
		}

		/* copio nel file i dati allegati nel messaggio 200 */
		do {

#ifdef DEBUG
printf("Controllo su thread: %ld. Do-While buffer_data_to_file.\n", pthread_self());
fflush(stdout);
#endif

			ris = write(fd, buf+len2+1, data_len);
		} while ((ris < 0) && (errno == EINTR));

		if (ris < 0) {
			printf("Chiamata a write() per cache file fallita, FD = %d. Errore: %d \"%s\"\n", fd, errno,strerror(errno));
			fflush(stdout);
			/***************************************************/
			close(fd);
			/***************************************************/
			remove(path);
			return (-1);
		}

		/***************************************************/
		close(fd);
		/***************************************************/
		return(expire); /* messaggio 200 corretto e completo, messo nel file, torno tempo di expire */
									
	}	
	return(-1); /* Incompleto */

}
