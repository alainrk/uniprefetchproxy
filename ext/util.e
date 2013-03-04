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

#ifndef __UTIL_E__
#define __UTIL_E__

#include "const.h"
#include "struct.h"

extern int mysend (int fd, const void *buf, size_t n);

extern int myread (int fd, char *ptr, int nbytes);

extern int TCP_setup_socket_listening(int *plistenfd, int numero_porta_locale);

extern int SetsockoptReuseAddr(int s);

extern int getDuePunti(struct Request *preq, int *plen);

extern int getSlash(struct Request *preq, int *plen);

extern int getEOL(struct Request *preq, int *plen);

extern int getPunto(struct Request *preq, int *plen);

extern int getMeno(struct Request *preq, int *plen);

extern int getInt(struct Request *preq, int *plen, int *pint);

extern int getIP(struct Request *preq, int *plen);

extern int getPort(struct Request *preq, int *plen);

extern int getPathAndEOL(struct Request *preq, int *plen);

extern int getRangeAndEOL(struct Request *preq, int *plen);

extern int checkRequest(struct Request *preq);

extern int getRequest(int fd, struct Request *preq);

extern void initRequest(struct Request *preq);

extern void exitThread();

extern int getFileInfo(char *filename, struct FileInfo *pfi);

extern int AppendBytesFromFile(char *filename, struct Request *preq, struct Response *presp);

extern int prepareGetResponse(struct Request *preq, struct Response *presp, const char *name);

extern int getPathAndMAGGIORE_onresp(char *buf, int len, int *plen, char *path);

extern int getDuePunti_onresp(char *buf, int len, int *plen);

extern int getSlash_onresp(char *buf, int len, int *plen);

extern int getPunto_onresp(char *buf, int len, int *plen);

extern int getPuntoEVirgola_onresp(char *buf, int len, int *plen);

extern int getInt_onresp(char *buf, int len, int *plen, int *pint);

extern int getIP_onresp(char *buf, int len, int *plen, struct in_addr *pinaddr);

extern int getPort_onresp(char *buf, int len, int *plen, int *pport);

extern int parseINF(char *buf, int len);

extern int parseRANGE(char *buf, int len);

extern int parse(char *buf, int *plenparsed, int len, struct Request *p_prefetch_req, int level);

#endif
