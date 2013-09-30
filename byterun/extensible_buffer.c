/* Luca Saiu, REENTRANTRUNTIME */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlvalues.h"
#include "context.h"
#include "memory.h"
#include "context.h"
#include "mlvalues.h"
#include "extensible_buffer.h"

#ifdef HAS_PTHREAD
#define Lock(shape_pointer) do { pthread_mutex_lock(& (shape_pointer)->mutex); } while(0)
#define Unlock(shape_pointer) do { pthread_mutex_unlock(& (shape_pointer)->mutex); } while(0)
#else
#define Lock(shape_pointer) do {} while(0)
#define Unlock(shape_pointer) do {} while(0)
#endif // #ifdef HAS_PTHREAD

void caml_initialize_extensible_buffer_shape(struct caml_extensible_buffer_shape *shape, char initial_value){
  shape->used_size = 0;
  shape->initial_value = initial_value;
#ifdef HAS_PTHREAD
  pthread_mutex_init(& shape->mutex, NULL);
#endif // #ifdef HAS_PTHREAD
}

void caml_finalize_extensible_buffer_shape(struct caml_extensible_buffer_shape *shape){
#ifdef HAS_PTHREAD
  pthread_mutex_destroy(& shape->mutex);
#endif // #ifdef HAS_PTHREAD
  /* Invalidate the content with some easy-to-recoognize value: */
  memset(shape, 0xab, sizeof(struct caml_extensible_buffer_shape));
}

void caml_initialize_extensible_buffer(struct caml_extensible_buffer *extensible_buffer,
                                       struct caml_extensible_buffer_shape *shared_shape){
  size_t allocated_size;

  Lock(shared_shape);
  allocated_size = shared_shape->used_size * 2 + 8; /* at least a few words */
  extensible_buffer->local_used_size = shared_shape->used_size;
  Unlock(shared_shape);

  extensible_buffer->allocated_size = allocated_size;
  extensible_buffer->array = caml_stat_alloc(allocated_size);;
  memset(extensible_buffer->array, shared_shape->initial_value, extensible_buffer->local_used_size);
  extensible_buffer->shared_shape = shared_shape;
}
void caml_finalize_extensible_buffer(struct caml_extensible_buffer *extensible_buffer){
  free(extensible_buffer->array);
  /* Invalidate the content with some easy-to-recoognize value: */
  memset(extensible_buffer, 0xab, sizeof(struct caml_extensible_buffer));
}

static void caml_reallocate_extensible_buffer(struct caml_extensible_buffer *b, size_t new_allocated_size){
  //size_t old_allocated_size = b->allocated_size;
  b->array = caml_stat_resize(b->array, new_allocated_size);
  b->allocated_size = (long)new_allocated_size;

  /* We leave the new part, if any, uninitialized.  We're going to
     initialize it when the space is actually used. */
}

static void caml_reallocate_extensible_buffer_if_needed(struct caml_extensible_buffer *b, size_t new_used_size){
  size_t new_allocated_size = b->allocated_size;

  while(new_allocated_size < new_used_size)
    new_allocated_size = new_allocated_size * 2 + 8;
  caml_reallocate_extensible_buffer(b, new_allocated_size);
}

void caml_resize_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t new_used_size){
  struct caml_extensible_buffer_shape *shape = b->shared_shape;

  /* Add enough padding bytes in the end to make the next element,
     potentially a pointer, at least word-aligned.  This is required
     on some architectures, and yields better performance in any case.
     This element is already guaranteed to begin at a word-aligned
     address. */
  const size_t word_size = sizeof(void*);
  const size_t misalignment = new_used_size % word_size;
  if(misalignment != 0)
    new_used_size += word_size - misalignment;

  /* Make the allocated buffer large enough, if needed: */
  caml_reallocate_extensible_buffer_if_needed(b, new_used_size);

  /* Update the used size and initialize the newly-used part, if any: */
  Lock(shape);
  if(new_used_size > shape->used_size){
    memset(((char*)b->array) + shape->used_size,
           shape->initial_value,
           new_used_size - shape->used_size);
    //fprintf(stderr, "+++++++++++++Initialized with %i from %i to %i\n", initial_value, b->used_size, new_used_size - 1);
  }
  shape->used_size = new_used_size;
  Unlock(shape);
  b->local_used_size = new_used_size;
}

size_t caml_allocate_from_extensible_buffer(struct caml_extensible_buffer *b,
                                            size_t new_element_size){
  const size_t word_size = sizeof(void*);
  struct caml_extensible_buffer_shape *shape = b->shared_shape;
  size_t beginning_of_this_element;

  Lock(shape);
  beginning_of_this_element = (size_t)shape->used_size;

  /* Reallocate the global array if needed: */
  caml_reallocate_extensible_buffer_if_needed(b, beginning_of_this_element + new_element_size);
  //while((beginning_of_this_element + new_element_size) > b->allocated_size)
  //  caml_reallocate_extensible_buffer(b, b->allocated_size * 2 + 1, initial_value);

  long new_used_size = shape->used_size + new_element_size;

  /* Make sure the used size remains a multiple of the word size, so
     that the next allocated object will be word-aligned; that's
     required on some architectures, and not mandatory but important
     for performance on others: */
  size_t misalignment = new_used_size % word_size;
  if(misalignment != 0)
    new_used_size += word_size - misalignment;

  /* Fill the newly-allocated part: */
  memset(((char*)b->array) + shape->used_size, shape->initial_value, new_used_size - shape->used_size);
  //fprintf(stderr, "+++++++++++++Initialized with %i from %i to %i\n", initial_value, b->used_size, new_used_size - 1);

  shape->used_size = new_used_size;
  Unlock(shape);
  b->local_used_size = new_used_size;

  //printf("%p->used_size is now %i bytes (%i words)\n", (int)b->used_size, (((int)(b->used_size)) / sizeof(void*)));
  return beginning_of_this_element;
}
