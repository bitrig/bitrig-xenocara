/*
 * Mesa 3-D graphics library
 * Version:  6.5
 *
 * Copyright (C) 2005-2006  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined SLANG_UTILITY_H
#define SLANG_UTILITY_H

#if defined __cplusplus
extern "C" {
#endif

/* Compile-time assertions.  If the expression is zero, try to declare an
 * array of size [-1] to cause compilation error.
 */
#define static_assert(expr) do { int _array[(expr) ? 1 : -1]; (void) _array[0]; } while (0)

#define slang_alloc_free(ptr) _mesa_free (ptr)
#define slang_alloc_malloc(size) _mesa_malloc (size)
#define slang_alloc_realloc(ptr, old_size, size) _mesa_realloc (ptr, old_size, size)
#define slang_string_compare(str1, str2) _mesa_strcmp (str1, str2)
#define slang_string_copy(dst, src) _mesa_strcpy (dst, src)
#define slang_string_duplicate(src) _mesa_strdup (src)
#define slang_string_length(str) _mesa_strlen (str)

char *slang_string_concat (char *, const char *);

typedef GLvoid *slang_atom;

#define SLANG_ATOM_NULL ((slang_atom) 0)

typedef struct slang_atom_entry_
{
	char *id;
	struct slang_atom_entry_ *next;
} slang_atom_entry;

#define SLANG_ATOM_POOL_SIZE 1023

typedef struct slang_atom_pool_
{
	slang_atom_entry *entries[SLANG_ATOM_POOL_SIZE];
} slang_atom_pool;

GLvoid slang_atom_pool_construct (slang_atom_pool *);
GLvoid slang_atom_pool_destruct (slang_atom_pool *);
slang_atom slang_atom_pool_atom (slang_atom_pool *, const char *);
const char *slang_atom_pool_id (slang_atom_pool *, slang_atom);

#ifdef __cplusplus
}
#endif

#endif

