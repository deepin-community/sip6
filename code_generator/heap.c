/*
 * Wrappers around standard functions that use the heap.
 *
 * Copyright (c) 2022 Riverbank Computing Limited <info@riverbankcomputing.com>
 *
 * This file is part of SIP.
 *
 * This copy of SIP is licensed for use under the terms of the SIP License
 * Agreement.  See the file LICENSE for more details.
 *
 * This copy of SIP may also used under the terms of the GNU General Public
 * License v2 or v3 as published by the Free Software Foundation which can be
 * found in the files LICENSE-GPL2 and LICENSE-GPL3 included in this package.
 *
 * SIP is supplied WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */


#undef NDEBUG
#include <assert.h>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#include "sip.h"


/*
 * Wrap malloc() and handle any errors.
 */
void *sipMalloc(size_t n)
{
	void *h;

	h = malloc(n);
    assert(h != NULL);

    memset(h, 0, n);

	return h;
}


/*
 * Wrap calloc() and handle any errors.
 */
void *sipCalloc(size_t nr, size_t n)
{
	void *h;

	h = calloc(nr, n);
    assert(h != NULL);

    return h;
}


/*
 * Wrap strdup() and handle any errors.
 */
char *sipStrdup(const char *s)
{
	char *h;

	h = strdup(s);
    assert(h != NULL);

	return h;
}


/*
 * Return a string on the heap which is the concatenation of all the arguments.
 */
char *concat(const char *s, ...)
{
	const char *sp;
	char *new;
	size_t len;
	va_list ap;

	/* Find the length of the final string. */

	len = 1;
	va_start(ap,s);

	for (sp = s; sp != NULL; sp = va_arg(ap, const char *))
		len += strlen(sp);

	va_end(ap);

	/* Create the new string. */

	new = sipMalloc(len);
	*new = '\0';

	va_start(ap,s);

	for (sp = s; sp != NULL; sp = va_arg(ap, const char *))
		strcat(new,sp);

	va_end(ap);

	return new;
}


/*
 * Append a string to another that is on the heap.
 */
void append(char **s, const char *new)
{
	*s = realloc(*s, strlen(*s) + strlen(new) + 1);
    assert(*s != NULL);

	strcat(*s, new);
}
