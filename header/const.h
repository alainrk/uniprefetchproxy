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

#ifndef __CONST_H__
#define __CONST_H__

#define MAXLENPATH 												2048
#define MAXLENREQ 												((MAXLENPATH)+1024)
#define MAXLENDATA 												5000
#define MAXLENRESP 												((MAXLENREQ)+MAXLENDATA)
#define GET 															1
#define INF 															2

#define INACTIVITY_TIMEOUT_SECONDS 				3
#define CHANCE 														10								/* CHANCE*INACTIVITY_TIMEOUT_SECONDS secondi di attesa massima */
#define SERVER_CHANCE 										10
#define MAXNUMTHREADWORKING 							100

#define HIDDEN 														static

#define FIRST_SECOND_LEVEL_PREFETCHING 		0
#define THIRD_LEVEL_PREFETCHING 					1

#define MIN_BYTE_BEFORE_DATA 20

/* Rende più stabile la ricerca di pagine già presenti in cache, ma ovviamente rallenta */
#define MUTEX_CACHE

/* Attiva il thread demone per la pulizia della cache */
/*#define DAEMON_CACHE_CLEAN*/

/* Abilita printf debugging */
#define DEBUG

#endif
