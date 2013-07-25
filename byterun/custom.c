/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Manuel Serrano and Xavier Leroy, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2000 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#include <stdio.h>
#include <string.h>

#define CAML_CONTEXT_ROOTS

#include "alloc.h"
#include "custom.h"
#include "fail.h"
#include "memory.h"
#include "mlvalues.h"
#include "io.h"

CAMLexport value caml_alloc_custom(struct custom_operations * ops,
                                   uintnat size,
                                   mlsize_t mem,
                                   mlsize_t max)
{
  mlsize_t wosize;
  value result;
  INIT_CAML_R;

  wosize = 1 + (size + sizeof(value) - 1) / sizeof(value);
  if (ops->finalize == NULL && wosize <= Max_young_wosize) {
    result = caml_alloc_small_r(ctx, wosize, Custom_tag);
    Custom_ops_val(result) = ops;
  } else {
    result = caml_alloc_shr_r(ctx, wosize, Custom_tag);
    Custom_ops_val(result) = ops;
    caml_adjust_gc_speed_r(ctx, mem, max);
    result = caml_check_urgent_gc_r(ctx, result);
  }
  return result;
}

struct custom_operations_list {
  struct custom_operations * ops;
  struct custom_operations_list * next;
};

/* This can be safely shared among contexts, so I'm keeping it as a
   global -- that's ok.  FIXME: but shouldn't we protect accesses with
   a global mutex?  --Luca Saiu, REENTRANTCONTEXT: */
static struct custom_operations_list * custom_ops_table = NULL;

// FIXME: again: mutex? --Luca Saiu, REENTRANTCONTEXT
CAMLexport void caml_register_custom_operations(struct custom_operations * ops)
{
  struct custom_operations_list * l =
    caml_stat_alloc(sizeof(struct custom_operations_list));
  Assert(ops->identifier != NULL);
  Assert(ops->deserialize != NULL);
  l->ops = ops;
  l->next = custom_ops_table;
  custom_ops_table = l;
}

// FIXME: again: mutex? --Luca Saiu, REENTRANTCONTEXT
struct custom_operations * caml_find_custom_operations(char * ident)
{
  struct custom_operations_list * l;
  for (l = custom_ops_table; l != NULL; l = l->next)
    if (strcmp(l->ops->identifier, ident) == 0) return l->ops;
  return NULL;
}

// FIXME: again: mutex? --Luca Saiu, REENTRANTCONTEXT
static struct custom_operations_list * custom_ops_final_table = NULL;

// FIXME: again: mutex? --Luca Saiu, REENTRANTCONTEXT
struct custom_operations * caml_final_custom_operations(final_fun fn)
{
  struct custom_operations_list * l;
  struct custom_operations * ops;
  for (l = custom_ops_final_table; l != NULL; l = l->next)
    if (l->ops->finalize == fn) return l->ops;
  ops = caml_stat_alloc(sizeof(struct custom_operations));
  ops->identifier = "_final";
  ops->finalize = fn;
  ops->compare = custom_compare_default;
  ops->hash = custom_hash_default;
  ops->serialize = custom_serialize_default;
  ops->deserialize = custom_deserialize_default;
  ops->compare_ext = custom_compare_ext_default;
  ops->cross_context_serialize = custom_serialize_default;
  ops->cross_context_deserialize = custom_deserialize_default;
  l = caml_stat_alloc(sizeof(struct custom_operations_list));
  l->ops = ops;
  l->next = custom_ops_final_table;
  custom_ops_final_table = l;
  return ops;
}

extern struct custom_operations caml_int32_ops,
                                caml_nativeint_ops,
                                caml_int64_ops;

void caml_init_custom_operations(void)
{
  //INIT_CAML_R; DUMP("Initializing file locking functions");
  caml_initialize_default_channel_mutex_functions();
  //DUMP("Initializing custom operations, including channels");
  caml_register_custom_operations(&caml_int32_ops);
  caml_register_custom_operations(&caml_nativeint_ops);
  caml_register_custom_operations(&caml_int64_ops);
  caml_register_custom_operations(&caml_channel_operations);
}
