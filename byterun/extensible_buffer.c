/* Luca Saiu, REENTRANTRUNTIME */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlvalues.h"
#include "memory.h"
#include "extensible_buffer.h"

static void caml_reallocate_extensible_buffer(struct caml_extensible_buffer *b, size_t new_allocated_size){
  //size_t old_allocated_size = b->allocated_size;
  b->array = caml_stat_resize(b->array, new_allocated_size);
  b->allocated_size = (long)new_allocated_size;

  /* /\* If we're growing the array, initialize the new part: *\/ */
  /* if(new_allocated_size > old_allocated_size) */
  /*   memset(((char*)b->array) + old_allocated_size, initial_value, new_allocated_size - old_allocated_size); */
  //fprintf(stderr, "The extensible buffer at %p has now allocated-size = %i bytes (%i words)\n", b, (int)new_allocated_size, (int)(new_allocated_size / sizeof(void*)));
}

static void caml_reallocate_extensible_buffer_if_needed(struct caml_extensible_buffer *b, size_t new_used_size){
  while(new_used_size > b->allocated_size)
    caml_reallocate_extensible_buffer(b, b->allocated_size * 2 + 1);
}

void caml_resize_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t new_used_size,
                                   char initial_value){
  /* Make the allocated buffer large enough, if needed: */
  //fprintf(stderr, "JJ [1] %i\n", (int)new_used_size);
  caml_reallocate_extensible_buffer_if_needed(b, new_used_size);
  //while(new_used_size > b->allocated_size)
  //  caml_reallocate_extensible_buffer(b, b->allocated_size * 2 + 1, initial_value);
  //fprintf(stderr, "JJ [2]\n");

  /* Update the used size and initialize the newly-used part, if any: */
  if(b->used_size < new_used_size)
    memset(((char*)b->array) + b->used_size, initial_value, new_used_size - b->used_size);
  b->used_size = new_used_size;
}

size_t caml_allocate_from_extensible_buffer(struct caml_extensible_buffer *b,
                                            size_t new_element_size,
                                            char initial_value){
  size_t beginning_of_this_element = (size_t)b->used_size;

  /* Reallocate the global array if needed: */
  caml_reallocate_extensible_buffer_if_needed(b, beginning_of_this_element + new_element_size);
  //while((beginning_of_this_element + new_element_size) > b->allocated_size)
  //  caml_reallocate_extensible_buffer(b, b->allocated_size * 2 + 1, initial_value);

  b->used_size += new_element_size;
  //printf("%p->used_size is now %i bytes (%i words)\n", (int)b->used_size, (((int)(b->used_size)) / sizeof(void*)));
  return beginning_of_this_element;
}

void caml_shrink_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t bytes_to_remove){
  b->used_size -= bytes_to_remove;
}
