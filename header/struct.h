/************************************************************************

	Copyright (C) Alain Di Chiappari	2011
	alain.dichiappari [at] studio [dot] unibo [dot] it

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

#ifndef __STRUCT_H__
#define __STRUCT_H__

#include "const.h"


struct Range {
	int first; 	/* -1 = NON SPECIFICATO */
	int last;		/* -1 = NON SPECIFICATO/FINO A FINE FILE */
};

struct Request {
	char buf[MAXLENREQ];
	int lenreq;		
	int reqType;						/* Usato anche per chiedere prefetching di 1°-2° o 3° livello quando passato a thread */
	uint16_t port;
	struct in_addr inaddr; /* network endianess */
	char path[MAXLENPATH];
	struct Range range;
	short int is_range;		/* Utile in prefetching per sapere velocemente se richiesta è RANGE (campi di range vengono modificati nella PrepareGetResponse) */
};

struct Response {
	char buf[MAXLENRESP];
	int lenresp;
};

struct param {
	int fd;
};

struct FileInfo {
	int error; /* 1 ok */
	int expire;
	long int len;
};

struct file_cache;
struct file_cache {
	char URL[MAXLENREQ]; 					/* Essendo univoco per definizione, mi va benissimo come hash di un file */
	char cache_name[MAXLENPATH];	/* Nome random sul proxy */
	int expire;										/* "Data" di scadenza in secondi */
	struct file_cache *next;			/* Puntatore per lista */
};

#endif
