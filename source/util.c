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

/* Utili.c */


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

#include "proxy.e"

int mysend (int fd, const void *buf, size_t n) {
   
	size_t	nleft;     
	ssize_t  nwritten;  
	char	*ptr;
	int chance = CHANCE;

	ptr = (void *)buf;
	nleft = n;
	while (nleft > 0) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While mysend().\n", pthread_self());
fflush(stdout);
#endif

		if ( (nwritten = send(fd, ptr, nleft, MSG_NOSIGNAL)) < 0) {

			if (errno == EINTR)	nwritten = 0;   /* send() again */

			else if (((errno == EAGAIN) || (errno == EWOULDBLOCK)) && (chance > 0)) {
				chance--;
				nwritten = 0;
			}

			else if (chance <= 0) 
				return(-2); /* time out */

			else
				return(-1); /* error */
		}

		/* Caso ok o EINTR/EAGAIN/EWOULDBLOCK, avanzo */
		nleft -= nwritten;
		ptr   += nwritten;

	}

	return(n);
}


int myread (int fd, char *ptr, int nbytes) {

	int nleft,nread,totread=0;
	int chance = CHANCE;

	nleft=nbytes;

	while(nleft > 0) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While myread().\n", pthread_self());
fflush(stdout);
#endif

		do {

#ifdef DEBUG
printf("Controllo su thread: %ld. Do-While myread().\n", pthread_self());
fflush(stdout);
#endif

			nread=read(fd,ptr,nleft);
		} while ( (nread<0) && (errno==EINTR));

		if ((nread < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
			char msg[2000];
			sprintf(msg,"readn: errore in lettura [result %d] :",nread);
			perror(msg);
			return(-1);
		}

		else if ((nread < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)) && (chance > 0)) {
			chance--;
			nread = 0;
		}
	
		else if (chance <= 0) 
				return(-2); /* time out */

		else if(nread == 0)
				return(totread); /* assumo che quando il server mi ha mandato tutto chiude, e read torna 0 */

		totread += nread;
		nleft -= nread;
		ptr   += nread;
	}

return(totread);

}


int TCP_setup_socket_listening(int *plistenfd, int numero_porta_locale) {

	int ris;
	struct sockaddr_in Local;
	
	*plistenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (*plistenfd < 0)
	{	
		printf ("socket() failed, Err: %d \"%s\"\n", errno,strerror(errno));
		fflush(stdout);
		return(0);
	}

	/*name the socket */
	memset ( &Local, 0, sizeof(Local) );
	Local.sin_family= AF_INET;
	/* specifico l'indirizzo IP attraverso cui voglio ricevere la connessione */
	Local.sin_addr.s_addr = htonl(INADDR_ANY);
	Local.sin_port	      = htons(numero_porta_locale);

	ris = bind(*plistenfd, (struct sockaddr*) &Local, sizeof(Local));
	if (ris<0)
	{	
		printf ("bind() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		fflush(stderr);
		return(0);
	}

	/* enable accepting of connection  */
	ris = listen(*plistenfd, 100 );
	if (ris<0)
	{	
		printf ("listen() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		fflush(stdout);
		exit(1);
	}
	return(1);
}


int	SetsockoptReuseAddr(int s)
{
	int OptVal, ris, myerrno;

	/* avoid EADDRINUSE error on bind() */
	OptVal = 1;
	ris = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&OptVal, sizeof(OptVal));
	if (ris != 0 )  {
		myerrno=errno;
		printf ("setsockopt() SO_REUSEADDR failed, Err: %d \"%s\"\n", errno,strerror(errno));
		fflush(stdout);
		errno=myerrno;
		return(0);
	}
	else
		return(1);
}


int getDuePunti(struct Request *preq, int *plen) {
	/* cerco un : */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != ':' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getSlash(struct Request *preq, int *plen) {
	/* cerco un / */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '/' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getEOL(struct Request *preq, int *plen) {
	/* cerco un \n */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '\n' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getPunto(struct Request *preq, int *plen) {
	/* cerco un . */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '.' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getMeno(struct Request *preq, int *plen) {
	/* cerco un . */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '-' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getInt(struct Request *preq, int *plen, int *pint) {
	/* cerco un intero */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(preq->buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if( (*pint<0) || (*pint>255) ) return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}


int getIP(struct Request *preq, int *plen) {

	int n, ris, newlen=*plen;
	char strIP[32];
	struct in_addr ina;

	/* cerco primo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);

	/* cerco secondo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);

	/* cerco terzo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);

	/* cerco quarto byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);

	/* Trovato un addr IP completo e lo salvo nel campo opportuno */
	memcpy(strIP,preq->buf+(*plen), newlen-(*plen) );
	strIP[newlen-(*plen)]=0;

	ris=inet_aton(strIP,&ina);
	if(ris==0) {
		perror("inet_aton failed :");
		return(-1); /* no IP */
	}

	memcpy(&(preq->inaddr),&ina,sizeof(struct in_addr));
	*plen=newlen;
	return(1);
}

int getPort(struct Request *preq, int *plen) {

	int port;

	if( (preq->lenreq - *plen) < 1 ) return(0); /* Richiesta incompleta */
	else {
		int ris; char str[128];

		/* Cerco un intero -> se mi da errore non lo è */
		ris=sscanf(preq->buf + (*plen), "%i", &port );
		if(ris!=1)	return(-1); /* errato */

		if( (port<0) || (port>65535) ) return(-1); /* errato, porta non possibile */

		/* Estrazione informazione e metto in campo opportuno */
		preq->port=(uint16_t)port;

		sprintf(str,"%i", port );
		*plen += strlen(str);
		return(1); /* found */
	}
}



int getPathAndEOL(struct Request *preq, int *plen) {

	int n=0;

	/* Scandisco finchè trovo un EOL, quindi ci sono */
	while(*plen+n < preq->lenreq) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While getPathAndEOL().\n", pthread_self());
fflush(stdout);
#endif

		if(preq->buf[*plen+n] == '\n' ) {
			/* Terminatore e setto campo opportuno della struttura per richiesta */
			preq->path[n] = 0;
			*plen += n+1;
			return(1);
		}
		/* Avanzo e inserisco man mano caratteri nel campo path */
		else {
			preq->path[n] = preq->buf[*plen+n];
			n++;
		}
	}
	/* not found \n means incompleto */
	return(0);
}


int getRangeAndEOL(struct Request *preq, int *plen) {

#define RANGE  "Range "
	int ris, first, last;

	if( (preq->lenreq - *plen) <= strlen(RANGE)) {
		return(0); /* mancano caratteri */
	}
	if(strncmp(RANGE,preq->buf+(*plen),strlen(RANGE))==0) { /* found "Range " */
		/* RANGE */
		*plen += strlen(RANGE);

		ris=getInt(preq,plen, &first);
		if(ris!=1) return(ris); /* manca intero: o c'e' altro o e' finito buffer */

		preq->range.first=first;
		ris=getMeno(preq,plen);
		if(ris!=1) return(ris); /* manca -  : o c'e' altro o e' finito buffer */

		/* se c'e' EOL allora last e' non definito 
		 * quindi prima cerco un EOL 
		 */
		ris=getEOL(preq,plen);
		if( ris==0 ) { /* buffer finito */
			return(0);
		}
		else if( ris==1 ) { /* trovato EOL, last non definito */
			preq->range.last=-1;
			return(1); /* found Range con last non definito */
		}
		/* else ris==-1 trovato qualcosa di diverso da EOL, forse e' il last ? */
		ris=getInt(preq,plen, &last);
		if(ris!=1) return(ris); /* manca intero: o c'e' altro o e' finito buffer */
		preq->range.last=last;

		ris=getEOL(preq,plen);
		if(ris!=1) return(ris); /* se ris==0 fine buffer  se ris==-1 e' robaccia */

		return(1); /* found Range completo */
	}

	return(0); /* non dovrei arrivarci mai */
}


#define GETMHTTP "GET mhttp://"
#define INFMHTTP "INF mhttp://"
int checkRequest(struct Request *preq) {
	/*	restituisce 0 se lettura incompleta, 
	 *	-1 se formato errato, 1 se formato corretto e completo
	 */

	int ris, len;

	preq->buf[preq->lenreq]=0;
					
	if(preq->lenreq <= strlen(GETMHTTP)) { 	/* non ho letto abbastanza bytes */

		/* Qualche confronto con gli header (vedi su, da sistemare) */

		if(strcmp(preq->buf, GETMHTTP) == 0) { 	/* found parte di GET */
			return(0); /* lettura incompleta, read again */
		}
		if(strcmp(preq->buf, INFMHTTP) == 0) { 	/* found parte di INF */
			return(0); /* lettura incompleta, read again */
		}

		/* se arrivo qui ho bytes diversi da GET e INF, formato errato */
		return(-1);
	}

	/* Se ho una richiesta con più caratteri */
	if(preq->lenreq > strlen(GETMHTTP)) {
		
		/* È una richiesta GET */
		if(strncmp(GETMHTTP,preq->buf,strlen(GETMHTTP)) == 0) {

			/* Settaggio campo type */
			preq->reqType=GET;

			len=strlen(GETMHTTP);

			/* Estrazione informazioni e avanzamento puntatore buffer richiesta */
			ris=getIP(preq,&len);
			if(ris!=1) return(ris);
			ris=getDuePunti(preq,&len);
			if(ris!=1) return(ris);
			ris=getPort(preq,&len);
			if(ris!=1) return(ris);
			ris=getSlash(preq,&len);
			if(ris!=1) return(ris);
			ris=getPathAndEOL(preq,&len);
			if(ris!=1) return(ris);

			/* il campo Range puo' esserci o no, quindi prima cerco un EOL */
			ris = getEOL(preq,&len);

			if( (ris==0) || (ris==1) ) {
				return(ris); /* se c'e' un eol o e' gia' finito il buffer -> return */
			}

			else { /* ris == -1 */
			/* else ci sono caratteri ma non c'e' l' EOL
			 * quindi cerco se c'c' un Range
			 */
				ris=getRangeAndEOL(preq,&len);
				if(ris!=1) return(ris);

				ris=getEOL(preq,&len);
				/* restituisce 0 se buffer corto, 1 se trova \n, -1 se non trova */
				return(ris);

			}
		}

		/* È una richiesta INF */
		else if(strncmp(INFMHTTP,preq->buf,strlen(INFMHTTP)) == 0) { 

			preq->reqType=INF;
			len=strlen(INFMHTTP);
			ris=getIP(preq,&len);
			if(ris!=1) return(ris);
			ris=getDuePunti(preq,&len);
			if(ris!=1) return(ris);
			ris=getPort(preq,&len);
			if(ris!=1) return(ris);
			ris=getSlash(preq,&len);
			if(ris!=1) return(ris);
			ris=getPathAndEOL(preq,&len);
			if(ris!=1) return(ris);
			ris=getEOL(preq,&len);
			/* restituisce 0 se buffer corto, 1 se trova \n, -1 se non trova */
			return(ris);
		}
		else {
			/* c'erano abbastanza caratteri, ma non ho trovato ne GET ne INF */
			return(-1);
		}
	}
	return(0); /* default - lettura incompleta */
}






int getRequest(int fd, struct Request *preq) {
	int ris;
	int chance = CHANCE;

	while(1) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While getRequest().\n", pthread_self());
fflush(stdout);
#endif

		/* Cerco di ricevere dati dal Client */
		ris = recv(fd, preq->buf+preq->lenreq, MAXLENREQ - preq->lenreq, MSG_NOSIGNAL);

		if(ris < 0) {
			/* Interruzione dal sistema, riprovo a leggere */
			if(errno == EINTR) ;
			/* Timeout scaduto */
			else if (((errno == EAGAIN) || (errno == EWOULDBLOCK)) && (chance > 0)) chance--;
			else return(-2); 			/* Altro errore in lettura o timer scaduti */
		}

		else if(ris == 0) { 		/* Client ha chiuso la connessione */
			return(-3);
		}

		else { 									/* Letto qualcosa, controllo */
			preq->lenreq+=ris;

			/* Estraggo le informazioni che mi servono */
			ris = checkRequest(preq);

			if(ris == 0) ; /* Richiesta incompleta, continuo a leggere */

			else if(ris == -1)
				return(-1); /* Ho un formato errato */

			else  /* Buono e completo il formato, posso continuare l'analisi */
				return(1);
		}
	}
	return(-1); /* Altro */
}





void initRequest(struct Request *preq) {
	memset( preq, 0 , sizeof(struct Request) );
	/* lenreq settato a zero indica nuova richiesta */
	preq->range.first=-1; /* -1 means not specified */
	preq->range.last=-1;
}


void exitThread() {
	pthread_mutex_lock(&mutex_num);
	numthread--;
	pthread_mutex_unlock(&mutex_num);
	pthread_exit(NULL);
}


int getFileInfo(char *filename, struct FileInfo *pfi) {

	FILE *f;
	int ris, ret;
	long inizio, fine;

	f=fopen(filename,"rb");
	if(f==NULL) return(-3); /* file not found */

	ret=-5;
	/* vado all'inizio e prendo la posizione corrente */
	ris=fseek(f,0L,SEEK_SET);
	if(ris<0) ret=-5; /* unknown error */
	else {
		inizio=ftell(f);
		if(inizio<0) ret=-5; /* unknown error */
		else {
			/* vado alla fine e prendo la posizione corrente */
			ris=fseek(f,0L,SEEK_END);
			if(ris<0) ret=-5; /* unknown error */
			else {

				fine=ftell(f);
				if(fine<0) ret=-5; /* unknown error */

				else {

					pfi->error=0;
					pfi->expire=30; /* 30 secs */
					pfi->len= (long int)(fine-inizio);
					ret=1;
				}
			}
		}
	}
	fclose(f);

	return( ret );
}


int AppendBytesFromFile(char *filename, struct Request *preq, struct Response *presp) {
	
	FILE *f;
	int ris, ret;

	f=fopen(filename,"rb");
	if(f==NULL) return(-3); /* file not found */

	ret=-5;
	/* vado all'inizio del range richiesto MA ATTENZIONE
	 * NEI FILE L'INDICE DEI CARATTERI COMINCIA DA 0
	 * MENTRE NEI RANGE IL PRIMO BYTE HA INDICE 1
	 * DA CUI IL -1 messo qui sotto
	 */
	ris=fseek(f, preq->range.first - 1 ,SEEK_SET);
	if(ris<0) ret=-5; /* unknown error */
	else {
		/* COPIA DEL FILE NEL BUFFER, partendo da dove sono arrivato con il completamento della risposta da inviare */
		ris = fread(presp->buf + presp->lenresp, 1, preq->range.last - preq->range.first +1, f);
		if( ris != (preq->range.last - preq->range.first + 1) ) {
			ret=-5; /* unknown error */
		}
		else {
			/* Incremento lunghezza bytes risposta */
			presp->lenresp += (preq->range.last - preq->range.first + 1); 
			/* un terminatore di stringhe in piu' non fa mai male */
			presp->buf[presp->lenresp]=0; 
			ret=1;
		}
	}
	fclose(f);

	return( ret );
}

/* Aggiungo il parametro per il nome perché devo trovarlo sul mio proxy */
int prepareGetResponse(struct Request *preq, struct Response *presp, const char *name) {

	int ris;
	struct FileInfo fi;
	char filename[MAXLENPATH];

	/* Creo il nome del file di cache sul proxy */
	strcpy(filename, "../cache/");
	strcat(filename, name);


	ris = getFileInfo(filename, &fi);
	if( 		(ris==-5) /* unknown error */
			||	(ris==-3)	/* file not found */
		)
		return(ris);

	else {	/* ris==1 */

		if(preq->range.first != -1 ) { /* e' una richiesta range */

			if( preq->range.first > fi.len ) { 
				return(-4); /* range fuori dal file */
			}
			if( preq->range.last > fi.len ) { 
				return(-4); /* range fuori dal file */
			}

			/* MI SERVE NEL PREFETCHING */
			preq->is_range = 1;

			if( (preq->range.last != -1) /* fine range e' definito */
					&& 
					(preq->range.last < preq->range.first) ) { 
				return(-4); /* range fuori dal file */
			}
			if( preq->range.last == -1 ) /* fine range e' fine file */
				preq->range.last = fi.len;

			/* compongo la risposta Range */
			sprintf(presp->buf,"201\nRange %i-%i\nExpire %i\n\n", 
							preq->range.first, preq->range.last, fi.expire);
			presp->lenresp=strlen(presp->buf);


			ris=AppendBytesFromFile(filename,preq,presp);
			if(ris!=1) {
				return(ris);
			}

			/* OK */
			return(1);

		}

		else { /* e' una richiesta di tutto il file */

			if( preq->range.last != -1) {
				return(-4); /* range errato */
			}

			/* MI SERVE NEL PREFETCHING */
			preq->is_range = 0;

			/* configuro il range */
			preq->range.first = 1;
			preq->range.last = fi.len;
			/* compongo la risposta GET */
			sprintf(presp->buf,"200\nLen %i\nExpire %i\n\n", 
							preq->range.last - preq->range.first +1, fi.expire);
			presp->lenresp=strlen(presp->buf);
			/* copio il range e incremento lenresp */
			ris=AppendBytesFromFile(filename,preq,presp);
			if(ris!=1) {
				return(ris); /* che minchia succede? */
			}
			return(1);

		}
	}
	return(-5); /* unknown error */
}

/* Usate in prefetching */
int getPathAndMAGGIORE_onresp(char *buf, int len, int *plen, char *path) {

	int n=0;
	while(*plen+n<len) {

#ifdef DEBUG
printf("Controllo su thread: %ld. While getPathAndMAGGIORE_onresp().\n", pthread_self());
fflush(stdout);
#endif

		if(buf[*plen+n] == '>' ) {
			path[n] = 0;
			*plen += n+1;
			return(1);
		}
		else {
			path[n] = buf[*plen+n];
			n++;
		}
	}
	/* not found \n means incompleto */
	return(0);
}

int getDuePunti_onresp(char *buf, int len, int *plen) {
	/* cerco un : */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != ':' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getSlash_onresp(char *buf, int len, int *plen) {
	/* cerco un / */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != '/' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}


int getPunto_onresp(char *buf, int len, int *plen) {
	/* cerco un . */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != '.' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getPuntoEVirgola_onresp(char *buf, int len, int *plen) {
	/* cerco un . */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != ';' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

int getInt_onresp(char *buf, int len, int *plen, int *pint) {
	/* cerco un intero */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if( (*pint<0) || (*pint>255) ) return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}


int getIP_onresp(char *buf, int len, int *plen, struct in_addr *pinaddr) {

	int n, ris, newlen=*plen;
	char strIP[32];
	struct in_addr ina;

	/* cerco primo byte */
	ris=getInt_onresp(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto_onresp(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco secondo byte */
	ris=getInt_onresp(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto_onresp(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco terzo byte */
	ris=getInt_onresp(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto_onresp(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco quarto byte */
	ris=getInt_onresp(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	/* ok, c'e' un indirizzo IP */
	memcpy(strIP,buf+(*plen), newlen-(*plen) );
	strIP[newlen-(*plen)]=0;
	ris=inet_aton(strIP,&ina);
	if(ris==0) {
		perror("inet_aton failed :");
		return(-1); /* no IP */
	}
	memcpy(pinaddr,&ina,sizeof(struct in_addr));
	*plen=newlen;
	return(1);
}

int getPort_onresp(char *buf, int len, int *plen, int *pport) {
	/* cerco un intero */
	
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(buf + (*plen), "%i", pport );
		if(ris!=1)	return(-1); /* errato */

		if( (*pport<0) || (*pport>65535) ) return(-1); /* errato */

		sprintf(str,"%i", *pport );
		*plen += strlen(str);
		return(1); /* found */
	}
}

/* Verificano in Session la correttezza di INF e RANGE */

/* Verifica che una risposta INF sia corretta, nel qual caso torna 0, altrimenti -1 */
int parseINF(char *buf, int len) {

	if(len<1) {
		return(-1);
	}

	if(len>=1) 
		if(buf[0]!='2') 
			return(-1);

	if(len>=2)
		if(buf[1]!='0')
			return(-1);
		
	if(len>=3) 
		if(buf[2]!='2')
			return(-1);
		
	if(len>=4)
		if(buf[3]!='\n')
			return(-1);

	/* Len NUM\n */
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
		
		/* Sembra ci sia tutto */
		return 0;

	}
return (-1);
}


/* Verifica che una risposta RANGE sia corretta, nel qual caso torna 0, altrimenti -1 */
int parseRANGE(char *buf, int len) {

	if(len<1) {
		return(-1);
	}

	if(len>=1) 
		if(buf[0]!='2') 
			return(-1);

	if(len>=2)
		if(buf[1]!='0')
			return(-1);
		
	if(len>=3) 
		if(buf[2]!='1')
			return(-1);
		
	if(len>=4)
		if(buf[3]!='\n')
			return(-1);

	/* Range Inizio-Fine\n */
	if(len>=5) 
		if(buf[4]!='R')
			return(-1);
		
	if(len>=6) 
		if(buf[5]!='a')
			return(-1);
		
	if(len>=7)
		if(buf[6]!='n')
			return(-1);

	if(len>=8)
		if(buf[7]!='g')
			return(-1);

	if(len>=9)
		if(buf[7]!='e')
			return(-1);

	if(len>=10)
		if(buf[7]!=' ')
			return(-1);

	if(len>=11) {

		int ris, len2, len3, len4;
		int inizio, fine;
		char str[1024];

		/* Inizio range */
		ris=sscanf(buf+10, "%i", &len2);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, inizio negativo */

		inizio = len2;

		sprintf(str,"%i", len2);
		/* len2=posizione, cerco il carattere successivo alla fine del numero trovato */
		len2=11+strlen(str);

		/* Linea range */
		if(len>=len2)
			if(buf[len2-1]!='-')
				return(-1);

		/* Fine range */
		ris=sscanf(buf+len2, "%i", &len3);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len3<0 ) return(-1); /* errato, fine negativa */

		fine = len3;

		sprintf(str,"%i", len3);

		len2=len2+1+strlen(str);
		
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

		ris=sscanf(buf+len2+7, "%i", &len4);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len3<0 ) return(-1); /* errato, expire negativo */

		sprintf(str,"%i", len4 );

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
			ma dovrebbero esserci ancora fine-inizio+1 byte
			di cui il primo char dovrebbe avere indice len2+2
	 */

		if( len < (len2+1+(fine-inizio+1)) )
			return(-1);
		
		/* Sembra ci sia tutto */
		return 0;

	}
return (-1);
}


/*
	Specifiche per il modulo prefetching.c
*/


/* 
	NB: USO CAMPO "buf[]" della struttura Request per l'URL
	cerca  <REF=mhttp://130.136.2.34:9876/piri/picchio>
*/
int isREF(char *buf, int i, int *plenparsed, int len, struct Request *p_prefetch_req){

	int ris;

#define REFMHTTP "REF=mhttp://"

	/* Se ha una lunghezza adeguata */
	if( ((strlen(REFMHTTP)) +1 /*nomefile almeno 1 char*/ +1 /* il > */ ) > (len-i) ) {
		/* formato errato */
		return(-1);
	}

	/* Se coincide con la stringa per REF */
	if( strncmp(REFMHTTP,buf+i,strlen(REFMHTTP)) == 0 ) {

		int len1=i+strlen(REFMHTTP);
		ris=getIP_onresp(buf,len,&len1,&(p_prefetch_req->inaddr));
		if(ris!=1) return(ris);
		ris=getDuePunti_onresp(buf,len,&len1);
		if(ris!=1) return(ris);
		ris=getPort_onresp(buf,len,&len1,(int *)&(p_prefetch_req->port));
		if(ris!=1) return(ris);
		ris=getSlash_onresp(buf,len,&len1);
		if(ris!=1) return(ris);
		ris=getPathAndMAGGIORE_onresp(buf,len,&len1,p_prefetch_req->path);
		if(ris!=1) return(ris);	/* Se ha una lunghezza adeguata */

		sprintf(p_prefetch_req->buf, "mhttp://%s:%i/%s",inet_ntoa(p_prefetch_req->inaddr), p_prefetch_req->port, p_prefetch_req->path);
		*plenparsed=len1;
		return(1);
	}
	else {
		/* NON e' un REF */
		return(-1);
	}

}

/* cerca  <IDX=1034;REF=mhttp://130.136.2.34:9876/piri/picchio> */
int isIDXREF(char *buf, int i, int *plenparsed, int len, struct Request *p_prefetch_req){

	int ris;
#define IDX "IDX="

	/* Se ha una lunghezza adeguata */
	if( (strlen(IDX) + 1 /* numero */ +1 /* ; */ + (strlen(REFMHTTP)) +1 /*nomefile*/ +1 /* il > */ ) > (len-i) ) {
		/* formato errato */
		return(-1);
	}

	/* Se coincide con la stringa per IDX;REF */
	if( strncmp(IDX,buf+i,strlen(IDX)) == 0 ) {
		int idx;
		int len1=i+strlen(IDX);
		/* in realta' non cerco una porta ma un numero intero */
		ris=getPort_onresp(buf,len,&len1,&idx);
		if(ris!=1) return(ris);
		ris=getPuntoEVirgola_onresp(buf,len,&len1);
		if(ris!=1) return(ris);

		/* ora cerco il REF= */
		if( isREF(buf, len1, plenparsed, len, p_prefetch_req) == 1 )
			return(1);

		else {
			/* NON e' un REF */
			return(-1);
		}
	}

	else {
		/* NON e' un REF */
		return(-1);
	}

}

/* NB: USO CAMPO "buf[]" della struttura Request per l'URL */
int parse(char *buf, int *plenparsed, int len, struct Request *p_prefetch_req, int level) {

	int i;

	/* plenparsed è inizializzato fuori, nel ciclo per scorrere tutto il buffer con la risposta */

	for( i=*plenparsed; i<len; i++ ) {

#ifdef DEBUG
printf("Controllo su thread: %ld. For nel parse().\n", pthread_self());
fflush(stdout);
#endif

		if(buf[i]=='<') {
		
			if(i+1<len) {
			
				/* passo al carattere successivo al < */
				++*plenparsed;
				i++;

				if( isREF(buf, i, plenparsed, len, p_prefetch_req) == 1 ) {
					/* ora *plenparsed e' l'indice del carattere 
					 * successivo al carattere > che termina REF 
						 Qui la funzione termina, session farà partire il prefetching
						 e poi richiamerà questa per andare avanti nel parsing.
					 */
					return(1);
				}

				else if( isIDXREF(buf, i, plenparsed, len, p_prefetch_req) == 1 ) {

					/* 
						ora *plenparsed indica il carattere > che termina IDXREF
					 	assegno ad i il valore di *plenparsed,
					 	MA LO DEVO DECREMENTARE DI 1 PERCHE' POI
					 	il for fa i++ e lo fa puntare al carattere successivo al >
					*/

					/* Se sono al 1° - 2° livello ora faccio tornare la funzione così da scaricare anche gli URL per IDX;REF */
					if (level == FIRST_SECOND_LEVEL_PREFETCHING)
						return(1);
					/* Altrimenti se sono al 3° livello voglio solo URL di sequenze REF e quindi proseguo */
					i=*plenparsed; 
					i--;
					/* continuo il parsing del carattere successivo al > */
				}
				else {
					/* errore */
					return(-1);
				}	
			}

			else { /* i+1 >= len  =>  errore, stringa finita prematuramente */
				return(-1);
			}


		}
		else {
			++*plenparsed;
		}
	}
	return(0); /* finito, ok */
}
