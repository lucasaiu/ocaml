/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* 1. Allocation functions doing the same work as the macros in the
      case where [Setup_for_gc] and [Restore_after_gc] are no-ops.
   2. Convenience functions related to allocation.
*/

#define CAML_CONTEXT_STARTUP
#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_MAJOR_GC
#define CAML_CONTEXT_MINOR_GC


#include <stdio.h> // FIXME: remove after debugging
#include <string.h>
#include "alloc.h"
#include "custom.h"
#include "major_gc.h"
#include "memory.h"
#include "mlvalues.h"
#include "stacks.h"

#define Setup_for_gc
#define Restore_after_gc

CAMLexport value caml_alloc_r (CAML_R, mlsize_t wosize, tag_t tag)
{
  value result;
  mlsize_t i;

  Assert (tag < 256);
  Assert (tag != Infix_tag);
  if (wosize == 0){
    result = Atom (tag);
  }else if (wosize <= Max_young_wosize){
    Alloc_small (result, wosize, tag);
    if (tag < No_scan_tag){
      for (i = 0; i < wosize; i++) Field (result, i) = 0;
    }
  }else{
    result = caml_alloc_shr_r (ctx, wosize, tag);
    if (tag < No_scan_tag) memset (Bp_val (result), 0, Bsize_wsize (wosize));
    result = caml_check_urgent_gc_r (ctx, result);
  }
  return result;
}

CAMLexport value caml_alloc_small_r (CAML_R, mlsize_t wosize, tag_t tag)
{
  value result;

  Assert (wosize > 0);
  Assert (wosize <= Max_young_wosize);
  Assert (tag < 256);
//result = NULL;fprintf(stderr, "caml_alloc_small_r: ctx %p, thread %p, wosize %i, tag %i\n", ctx, (void*)pthread_self(), (int)wosize, (int)tag); fflush(stderr);
  Alloc_small (result, wosize, tag);
  return result;
}

CAMLexport value caml_alloc_tuple_r(CAML_R, mlsize_t n)
{
  return caml_alloc_r(ctx, n, 0);
}

CAMLexport value caml_alloc_string_r (CAML_R, mlsize_t len)
{
  value result;
  mlsize_t offset_index;
  mlsize_t wosize = (len + sizeof (value)) / sizeof (value);

  if (wosize <= Max_young_wosize) {
    Alloc_small (result, wosize, String_tag);
  }else{
    result = caml_alloc_shr_r (ctx, wosize, String_tag);
    result = caml_check_urgent_gc_r (ctx, result);
  }
  Field (result, wosize - 1) = 0;
  offset_index = Bsize_wsize (wosize) - 1;
  Byte (result, offset_index) = offset_index - len;
  return result;
}

CAMLexport value caml_alloc_final_r (CAML_R, mlsize_t len, final_fun fun,
                                   mlsize_t mem, mlsize_t max)
{
  /* FIXME: this does not use ctx yet, since we didn't make an "_r" version of struct custom_operations yet.
     See byterun/custom.h --Luca Saiu, REENTRANTRUNTIME */
  return caml_alloc_custom(caml_final_custom_operations(fun),
                           len * sizeof(value), mem, max);
}

CAMLexport value caml_copy_string_r(CAML_R, char const *s)
{
  int len;
  value res;

  len = strlen(s);
  res = caml_alloc_string_r(ctx, len);
  memmove(String_val(res), s, len);
  return res;
}

CAMLexport value caml_alloc_array_r(CAML_R, value (*funct)(CAML_R, char const *),
                                  char const ** arr)
{
  CAMLparam0 ();
  mlsize_t nbr, n;
  CAMLlocal2 (v, result);

  nbr = 0;
  while (arr[nbr] != 0) nbr++;
  if (nbr == 0) {
    CAMLreturn (Atom(0));
  } else {
    result = caml_alloc_r (ctx, nbr, 0);
    for (n = 0; n < nbr; n++) {
      /* The two statements below must be separate because of evaluation
         order (don't take the address &Field(result, n) before
         calling funct, which may cause a GC and move result). */
      v = funct(ctx, arr[n]);
      caml_modify_r(ctx, &Field(result, n), v);
    }
    CAMLreturn (result);
  }
}

CAMLexport value caml_copy_string_array_r(CAML_R, char const ** arr)
{
  return caml_alloc_array_r(ctx, caml_copy_string_r, arr);
}

CAMLexport int caml_convert_flag_list(value list, int *flags)
{
  int res;
  res = 0;
  while (list != Val_int(0)) {
    res |= flags[Int_val(Field(list, 0))];
    list = Field(list, 1);
  }
  return res;
}

/* For compiling let rec over values */

CAMLprim value caml_alloc_dummy_r(CAML_R, value size)
{
  mlsize_t wosize = Int_val(size);

  if (wosize == 0) return Atom(0);
  return caml_alloc_r (ctx, wosize, 0);
}

CAMLprim value caml_alloc_dummy_float_r (CAML_R, value size)
{
  mlsize_t wosize = Int_val(size) * Double_wosize;

  if (wosize == 0) return Atom(0);
  return caml_alloc_r (ctx, wosize, 0);
}

CAMLprim value caml_update_dummy_r(CAML_R, value dummy, value newval)
{
  mlsize_t size, i;
  tag_t tag;

  size = Wosize_val(newval);
  tag = Tag_val (newval);
  Assert (size == Wosize_val(dummy));
  Assert (tag < No_scan_tag || tag == Double_array_tag);

  Tag_val(dummy) = tag;
  if (tag == Double_array_tag){
    size = Wosize_val (newval) / Double_wosize;
    for (i = 0; i < size; i++){
      Store_double_field (dummy, i, Double_field (newval, i));
    }
  }else{
    for (i = 0; i < size; i++){
      caml_modify_r (ctx, &Field(dummy, i), Field(newval, i));
    }
  }
  return Val_unit;
}

/* /\* // FIXME: remove this; this is just a test --Luca Saiu, reentrantruntime *\/ */
/* /\* CAMLprim___ value caml_alloc_dummy(value size){ *\/ */
/* /\*   printf(">>caml_alloc_dummy<<\n"); *\/ */
/* /\*   return size; *\/ */
/* /\* } *\/ */
/* /\* CAMLprim___ value caml_alloc_dummy_float(value size){ *\/ */
/* /\*   printf(">>caml_alloc_dummy_float<<\n"); *\/ */
/* /\*   return size; *\/ */
/* /\* } *\/ */
/* /\* CAMLprim___ value caml_update_dummy(CAML_R, value dummy, value newval){ *\/ */
/* /\*   printf(">>caml_update_dummy<<\n"); *\/ */
/* /\*   return dummy; *\/ */
/* /\* } *\/ */
