/* Read symbolic links into a buffer without size limitation, relative to fd.

   Copyright (C) 2001, 2003-2004, 2007, 2009-2011 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert, Bruno Haible, and Jim Meyering.  */

#include <config.h>

#include "careadlinkat.h"

#include "allocator.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Define this independently so that stdint.h is not a prerequisite.  */
#ifndef SIZE_MAX
# define SIZE_MAX ((size_t) -1)
#endif

#ifndef SSIZE_MAX
# define SSIZE_MAX ((ssize_t) (SIZE_MAX / 2))
#endif

#if ! HAVE_READLINKAT
/* Ignore FD.  Get the symbolic link value of FILENAME and put it into
   BUFFER, with size BUFFER_SIZE.  This function acts like readlink
   but has readlinkat's signature.  */
ssize_t
careadlinkatcwd (int fd, char const *filename, char *buffer,
                 size_t buffer_size)
{
  (void) fd;
  return readlink (filename, buffer, buffer_size);
}
#endif

/* Forward declaration.  We want to #undef malloc before initializing
   this struct, but cannot do so until after all code that uses named
   fields from "allocator.h" has been compiled.  */
static struct allocator const standard_allocator;

/* Assuming the current directory is FD, get the symbolic link value
   of FILENAME as a null-terminated string and put it into a buffer.
   If FD is AT_FDCWD, FILENAME is interpreted relative to the current
   working directory, as in openat.

   If the link is small enough to fit into BUFFER put it there.
   BUFFER's size is BUFFER_SIZE, and BUFFER can be null
   if BUFFER_SIZE is zero.

   If the link is not small, put it into a dynamically allocated
   buffer managed by ALLOC.  It is the caller's responsibility to free
   the returned value if it is nonnull and is not BUFFER.  A null
   ALLOC stands for the standard allocator.

   The PREADLINKAT function specifies how to read links.

   If successful, return the buffer address; otherwise return NULL and
   set errno.  */

char *
careadlinkat (int fd, char const *filename,
              char *buffer, size_t buffer_size,
              struct allocator const *alloc,
              ssize_t (*preadlinkat) (int, char const *, char *, size_t))
{
  char *buf;
  size_t buf_size;
  size_t buf_size_max =
    SSIZE_MAX < SIZE_MAX ? (size_t) SSIZE_MAX + 1 : SIZE_MAX;
  char stack_buf[1024];

  if (! alloc)
    alloc = &standard_allocator;

  if (! buffer_size)
    {
      /* Allocate the initial buffer on the stack.  This way, in the
         common case of a symlink of small size, we get away with a
         single small malloc() instead of a big malloc() followed by a
         shrinking realloc().  */
      buffer = stack_buf;
      buffer_size = sizeof stack_buf;
    }

  buf = buffer;
  buf_size = buffer_size;

  do
    {
      /* Attempt to read the link into the current buffer.  */
      ssize_t link_length = preadlinkat (fd, filename, buf, buf_size);
      size_t link_size;
      if (link_length < 0)
        {
          /* On AIX 5L v5.3 and HP-UX 11i v2 04/09, readlink returns -1
             with errno == ERANGE if the buffer is too small.  */
          int readlinkat_errno = errno;
          if (readlinkat_errno != ERANGE)
            {
              if (buf != buffer)
                {
                  alloc->free (buf);
                  errno = readlinkat_errno;
                }
              return NULL;
            }
        }

      link_size = link_length;

      if (link_size < buf_size)
        {
          buf[link_size++] = '\0';

          if (buf == stack_buf)
            {
              char *b = (char *) alloc->malloc (link_size);
              if (! b)
                break;
              memcpy (b, buf, link_size);
              buf = b;
            }
          else if (link_size < buf_size && buf != buffer && alloc->realloc)
            {
              /* Shrink BUF before returning it.  */
              char *b = (char *) alloc->realloc (buf, link_size);
              if (b)
                buf = b;
            }

          return buf;
        }

      if (buf != buffer)
        alloc->free (buf);

      if (buf_size <= buf_size_max / 2)
        buf_size *= 2;
      else if (buf_size < buf_size_max)
        buf_size = buf_size_max;
      else
        break;
      buf = (char *) alloc->malloc (buf_size);
    }
  while (buf);

  if (alloc->die)
    alloc->die ();
  errno = ENOMEM;
  return NULL;
}

/* Use the system functions, not the gnulib overrides, because this
   module does not depend on GNU or POSIX semantics.  See comments
   above why this must occur here.  */
#undef malloc
#undef realloc

/* A standard allocator.  For now, only careadlinkat needs this, but
   perhaps it should be moved to the allocator module.  */
static struct allocator const standard_allocator =
  { malloc, realloc, free, NULL };