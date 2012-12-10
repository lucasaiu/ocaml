/* Luca Saiu, REENTRANTRUNTIME */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlvalues.h"
#include "memory.h"
#include "extensible_buffer.h"

void caml_resize_extensible_buffer(struct caml_extensible_buffer *b, size_t new_allocated_size, char initial_value){
  size_t old_allocated_size = b->allocated_size;
  b->array = caml_stat_resize(b->array, new_allocated_size);
  b->allocated_size = (long)new_allocated_size;
  
  /* If we're growing the array, initialize the new part: */
  if(new_allocated_size > old_allocated_size)
    memset(((char*)b->array) + old_allocated_size, initial_value, new_allocated_size - old_allocated_size);
  //fprintf(stderr, "The extensible buffer at %p has now allocated-size %i bytes (%i words)\n", b, (int)new_allocated_size, (int)(new_allocated_size / sizeof(void*)));
}

size_t caml_allocate_from_extensible_buffer(struct caml_extensible_buffer *b,
                                            size_t new_element_size,
                                            char initial_value){
  size_t beginning_of_this_element = (size_t)b->used_size;

  /* Resize the global array if needed: */
  while((beginning_of_this_element + new_element_size) > b->allocated_size)
    caml_resize_extensible_buffer(b, b->allocated_size * 2, initial_value);

  b->used_size += new_element_size;
  //printf("%p->used_size is now %i bytes (%i words)\n", (int)b->used_size, (((int)(b->used_size)) / sizeof(void*)));
  return beginning_of_this_element;
}

void caml_shrink_extensible_buffer(struct caml_extensible_buffer *b,
                                   size_t bytes_to_remove){
  b->used_size -= bytes_to_remove;
}
