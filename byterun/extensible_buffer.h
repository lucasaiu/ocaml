/* Luca Saiu, REENTRANTRUNTIME */

#ifndef CAML_EXTENSIBLE_BUFFER_H
#define CAML_EXTENSIBLE_BUFFER_H

/* A growable buffer from which one can allocate elements of possibly
   non-homogeneous size.  De-allocation is only possible in a LIFO
   fashion.
   All sizes are in bytes.  We intentionally define this structure in
   a header so that its fields and their order are exactly known, and
   they can be accessed from assembly code as well.  We do not use
   size_t just to ensure the structure fields are word-sized. */
struct caml_extensible_buffer{
  void *array;
  long allocated_size;
  long used_size;
};

/* Resize the buffer to a given used size, initializing bytes to the
   given value when growing. */
void caml_resize_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t new_used_size,
                                   char initial_value);

/* Reserve the given number of bytes from the given extensible buffer,
   (automatically growing it in terms of allocated size) it if needed
   (possibly to a larger size than immediately needed).  Return an
   index for the allocated element as a byte offset from the
   beginning. */
size_t caml_allocate_from_extensible_buffer(struct caml_extensible_buffer *b,
                                            size_t new_element_size,
                                            char initial_value);

/* Shrink the given extensible buffer by the given size (provided as a
   positive number).  The internal array is not re-allocated, but allocated
   space can be reused this way. */
void caml_shrink_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t bytes_to_remove);

#endif /* #ifndef CAML_EXTENSIBLE_BUFFER_H */
