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

#ifndef __CACHING_E__
#define __CACHING_E__

#include "const.h"
#include "struct.h"

extern void init_cache_list();

extern void my_tempname(char *buffer);

extern void add_file_cache(struct in_addr *inaddr_target, uint16_t port_target, const char *path_target, int expire_target, char *filename);

extern int get_cache_name(struct in_addr *inaddr_target, uint16_t port_target, const char *path_target, char *buffer);

extern int buffer_data_to_file (char *filename, char *buffer, int len);

extern struct file_cache *sentinel;

#endif
