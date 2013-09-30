/* Luca Saiu, REENTRANTRUNTIME */

#include "config.h"

#ifdef HAS_PTHREAD
#include <pthread.h>
#endif // #ifdef HAS_PTHREAD

#ifndef CAML_EXTENSIBLE_BUFFER_H
#define CAML_EXTENSIBLE_BUFFER_H

/* A growable buffer from which one can allocate elements of possibly
   non-homogeneous size.  Buffers can grow but are never supposed to
   shrink.

   The buffer "used" size (its conceptual size, usually smaller than
   the currently allocated block size) is shared among several
   caml_extensible_buffer instances: the idea is that all the
   extensible buffers sharing the same shape are "aligned" and
   conceptually contain the same fields in the same order, even if the
   most recently allocated part might be still uninitialized in some
   instances.  Whenever an element is allocated on some buffer, space
   for this element is conceptually made in all buffers sharing the
   same shape.

   A buffer can be accessed for reading and writing without any
   synchronization, but allocating new space first requires to
   synchronize with the shared shape structure.

   All sizes are in bytes.  We intentionally define this structure in
   a header so that its fields and their order are exactly known, and
   they can be accessed from assembly code as well.  We do not use
   size_t just to ensure the structure fields are word-sized. */
struct caml_extensible_buffer{
  void *array;
  long allocated_size;
  long local_used_size; // this is guaranteed to be less than or equal to the shape used size, and less than or equal to the size of the array
  struct caml_extensible_buffer_shape *shared_shape;
};
struct caml_extensible_buffer_shape{
  long used_size;
  char initial_value; // the byte used to fill unused space (important in the case of Caml objects)
#ifdef HAS_PTHREAD
  pthread_mutex_t mutex; // I use a void* so as not to have the exported interface depend on pthreads
#endif // #ifdef HAS_PTHREAD
};

/* Intialize or finalize a given shape, to be used by extensible buffers: */
void caml_initialize_extensible_buffer_shape(struct caml_extensible_buffer_shape *shared_shape, char initial_value);
void caml_finalize_extensible_buffer_shape(struct caml_extensible_buffer_shape *shared_shape);

/* Intialize or finalize a given extensible buffer, having it use the given shared shape: */
void caml_initialize_extensible_buffer(struct caml_extensible_buffer *extensible_buffer,
                                       struct caml_extensible_buffer_shape *shared_shape);
void caml_finalize_extensible_buffer(struct caml_extensible_buffer *extensible_buffer);

/* Resize the buffer to a given used size, initializing bytes to the
   given value when growing. */
void caml_resize_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t new_used_size);

/* Reserve the given number of bytes from the given extensible buffer,
   (automatically growing it in terms of allocated size) it if needed
   (possibly to a larger size than immediately needed).  Return an
   index for the allocated element as a byte offset from the
   beginning.  Base plus offset will make a word-aligned pointer. */
size_t caml_allocate_from_extensible_buffer(struct caml_extensible_buffer *b,
                                            size_t new_element_size);

void caml_destroy_extensible_buffer(struct caml_extensible_buffer *b);


#endif /* #ifndef CAML_EXTENSIBLE_BUFFER_H */
