/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Raising exceptions from C. */

#define CAML_CONTEXT_FAIL
#define CAML_CONTEXT_ROOTS

#include <stdio.h> // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#include <signal.h>
#include "alloc.h"
#include "fail.h"
#include "io.h"
#include "gc.h"
#include "memory.h"
#include "mlvalues.h"
#include "printexc.h"
#include "signals.h"
#include "stack.h"
#include "roots.h"

/* The globals holding predefined exceptions */

typedef value caml_generated_constant[1];

extern caml_generated_constant
  caml_exn_Out_of_memory,
  caml_exn_Sys_error,
  caml_exn_Failure,
  caml_exn_Invalid_argument,
  caml_exn_End_of_file,
  caml_exn_Division_by_zero,
  caml_exn_Not_found,
  caml_exn_Match_failure,
  caml_exn_Sys_blocked_io,
  caml_exn_Stack_overflow,
  caml_exn_Assert_failure,
  caml_exn_Undefined_recursive_module;
extern caml_generated_constant
  caml_bucket_Out_of_memory,
  caml_bucket_Stack_overflow;

/* Exception raising */

extern void caml_raise_exception_r (CAML_R, value bucket) Noreturn;

void caml_raise_r(CAML_R, value v)
{
  Unlock_exn();
  if (caml_exception_pointer == NULL) caml_fatal_uncaught_exception_r(ctx, v);

#ifndef Stack_grows_upwards
#define PUSHED_AFTER <
#else
#define PUSHED_AFTER >
#endif
  while (caml_local_roots != NULL &&
         (char *) caml_local_roots PUSHED_AFTER caml_exception_pointer) {
    caml_local_roots = caml_local_roots->next;
  }
#undef PUSHED_AFTER

  caml_raise_exception_r(ctx, v);
}

void caml_raise_constant_r(CAML_R, value tag)
{
  CAMLparam1 (tag);
  CAMLlocal1 (bucket);

  bucket = caml_alloc_small_r (ctx, 1, 0);
  Field(bucket, 0) = tag;
  caml_raise_r(ctx, bucket);
  CAMLnoreturn;
}

void caml_raise_with_arg_r(CAML_R, value tag, value arg)
{
  CAMLparam2 (tag, arg);
  CAMLlocal1 (bucket);

  bucket = caml_alloc_small_r (ctx, 2, 0);
  Field(bucket, 0) = tag;
  Field(bucket, 1) = arg;
  caml_raise_r(ctx, bucket);
  CAMLnoreturn;
}

void caml_raise_with_args_r(CAML_R, value tag, int nargs, value args[])
{
  CAMLparam1 (tag);
  CAMLxparamN (args, nargs);
  value bucket;
  int i;

  Assert(1 + nargs <= Max_young_wosize);
  bucket = caml_alloc_small_r (ctx, 1 + nargs, 0);
  Field(bucket, 0) = tag;
  for (i = 0; i < nargs; i++) Field(bucket, 1 + i) = args[i];
  caml_raise_r(ctx, bucket);
  CAMLnoreturn;
}

void caml_raise_with_string_r(CAML_R, value tag, char const *msg)
{
  caml_raise_with_arg_r(ctx, tag, caml_copy_string_r(ctx, msg));
}

void caml_failwith_r (CAML_R, char const *msg)
{
  caml_raise_with_string_r(ctx, (value) caml_exn_Failure, msg);
}

void caml_invalid_argument_r (CAML_R, char const *msg)
{
  caml_raise_with_string_r(ctx, (value) caml_exn_Invalid_argument, msg);
}

/* To raise [Out_of_memory], we can't use [caml_raise_constant],
   because it allocates and we're out of memory...
   We therefore use a statically-allocated bucket constructed
   by the ocamlopt linker.
   This works OK because the exception value for [Out_of_memory] is also
   statically allocated out of the heap.
   The same applies to Stack_overflow. */

void caml_raise_out_of_memory_r(CAML_R)
{
  caml_raise_r(ctx, (value) &caml_bucket_Out_of_memory);
}

void caml_raise_stack_overflow_r(CAML_R)
{
  caml_raise_r(ctx, (value) &caml_bucket_Stack_overflow);
}

void caml_raise_sys_error_r(CAML_R, value msg)
{
  caml_raise_with_arg_r(ctx, (value) caml_exn_Sys_error, msg);
}

void caml_raise_end_of_file_r(CAML_R)
{
  caml_raise_constant_r(ctx, (value) caml_exn_End_of_file);
}

void caml_raise_zero_divide_r(CAML_R)
{
  caml_raise_constant_r(ctx, (value) caml_exn_Division_by_zero);
}

void caml_raise_not_found_r(CAML_R)
{
  caml_raise_constant_r(ctx, (value) caml_exn_Not_found);
}

void caml_raise_sys_blocked_io_r(CAML_R)
{
  caml_raise_constant_r(ctx, (value) caml_exn_Sys_blocked_io);
}

/* We allocate statically the bucket for the exception because we can't
   do a GC before the exception is raised (lack of stack descriptors
   for the ccall to [caml_array_bound_error].  */

#define BOUND_MSG "index out of bounds"
#define BOUND_MSG_LEN (sizeof(BOUND_MSG) - 1)

static struct {
  header_t hdr;
  value exn;
  value arg;
} array_bound_error_bucket;

static struct {
  header_t hdr;
  char data[BOUND_MSG_LEN + sizeof(value)];
} array_bound_error_msg = { 0, BOUND_MSG };

void caml_array_bound_error_r(CAML_R)
{
  if (! array_bound_error_bucket_inited) {
    mlsize_t wosize = (BOUND_MSG_LEN + sizeof(value)) / sizeof(value);
    mlsize_t offset_index = Bsize_wsize(wosize) - 1;
    array_bound_error_msg.hdr = Make_header(wosize, String_tag, Caml_white);
    array_bound_error_msg.data[offset_index] = offset_index - BOUND_MSG_LEN;
    array_bound_error_bucket.hdr = Make_header(2, 0, Caml_white);
    array_bound_error_bucket.exn = (value) caml_exn_Invalid_argument;
    array_bound_error_bucket.arg = (value) array_bound_error_msg.data;
    array_bound_error_bucket_inited = 1;
    caml_page_table_add_r(ctx, In_static_data,
                        &array_bound_error_msg,
                        &array_bound_error_msg + 1);
    array_bound_error_bucket_inited = 1;
  }
  caml_raise_r(ctx, (value) &array_bound_error_bucket.exn);
}

int caml_is_special_exception_r(CAML_R, value exn) {
  return exn == (value) caml_exn_Match_failure
    || exn == (value) caml_exn_Assert_failure
    || exn == (value) caml_exn_Undefined_recursive_module;
}
