
/*
 * @COPYRIGHT@
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 */

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "structs/structs.h"
#include "structs/type/array.h"
#include "structs/type/string.h"
#include "util/typed_mem.h"

/*********************************************************************
		DYNAMICALLY ALLOCATED STRING TYPE
*********************************************************************/

int
structs_string_init(const struct structs_type *type, void *data)
{
	return (structs_string_binify(type, "", data, NULL, 0));
}

int
structs_string_equal(const struct structs_type *type,
	const void *v1, const void *v2)
{
	const int as_null = type->args[1].i;
	const char *const s1 = *((const char **)v1);
	const char *const s2 = *((const char **)v2);
	int empty1;
	int empty2;

	if (!as_null)
		return (strcmp(s1, s2) == 0);
	empty1 = (s1 == NULL) || *s1 == '\0';
	empty2 = (s2 == NULL) || *s2 == '\0';
	if (empty1 ^ empty2)
		return (0);
	if (empty1)
		return (1);
	return (strcmp(s1, s2) == 0);
}

char *
structs_string_ascify(const struct structs_type *type,
	const char *mtype, const void *data)
{
	const int as_null = type->args[1].i;
	const char *s = *((char **)data);

	if (as_null && s == NULL)
		s = "";
	return (STRDUP(mtype, s));
}

int
structs_string_binify(const struct structs_type *type,
	const char *ascii, void *data, char *ebuf, size_t emax)
{
	const char *mtype = type->args[0].s;
	const int as_null = type->args[1].i;
	char *s;

	if (as_null && *ascii == '\0')
		s = NULL;
	else if ((s = STRDUP(mtype, ascii)) == NULL)
		return (-1);
	*((char **)data) = s;
	return (0);
}

/*
 * This can be used by any type that wishes to encode its
 * value using its ASCII string representation.
 */
int
structs_string_encode(const struct structs_type *type, const char *mtype,
	struct structs_data *code, const void *data)
{
	if ((code->data = (u_char *) structs_get_string(type,
	    NULL, data, mtype)) == NULL)
		return (-1);
	code->length = strlen((char *)code->data) + 1;
	return (0);
}

/*
 * This can be used by any type that wishes to encode its
 * value using its ASCII string representation.
 */
int
structs_string_decode(const struct structs_type *type,
	const u_char *code, size_t cmax, void *data, char *ebuf, size_t emax)
{
	size_t slen;

	/* Determine length of string */
	for (slen = 0; slen < cmax && ((char *)code)[slen] != '\0'; slen++);
	if (slen == cmax) {
		strlcpy(ebuf, "encoded string is truncated", emax);
		errno = EINVAL;
		return (-1);
	}

	/* Set string value */
	if ((*type->binify)(type, (const char *)code, data, ebuf, emax) == -1)
		return (-1);

	/* Done */
	return (slen + 1);
}

void
structs_string_free(const struct structs_type *type, void *data)
{
	const char *mtype = type->args[0].s;
	char *const s = *((char **)data);

	if (s != NULL) {
		FREE(mtype, s);
		*((char **)data) = NULL;
	}
}

PD_EXPORT const struct structs_type structs_type_string
	= STRUCTS_STRING_TYPE(STRUCTS_TYPE_STRING_MTYPE, 0);
PD_EXPORT const struct structs_type structs_type_string_null
	= STRUCTS_STRING_TYPE(STRUCTS_TYPE_STRING_MTYPE, 1);

/*********************************************************************
		BOUNDED LENGTH STRING TYPE
*********************************************************************/

int
structs_bstring_equal(const struct structs_type *type,
	const void *v1, const void *v2)
{
	const char *const s1 = v1;
	const char *const s2 = v2;

	return (strcmp(s1, s2) == 0);
}

char *
structs_bstring_ascify(const struct structs_type *type,
	const char *mtype, const void *data)
{
	const char *const s = data;

	return (STRDUP(mtype, s));
}

int
structs_bstring_binify(const struct structs_type *type,
	const char *ascii, void *data, char *ebuf, size_t emax)
{
	const size_t alen = strlen(ascii);

	if (alen + 1 > type->size) {
		strlcpy(ebuf,
		    "string is too long for bounded length buffer", emax);
		return (-1);
	}
	memcpy(data, ascii, alen + 1);
	return (0);
}

