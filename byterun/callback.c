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

/* Callbacks from C to OCaml */

#define CAML_CONTEXT_CALLBACK
#define CAML_CONTEXT_STACKS
#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_FIX_CODE

#include <stdio.h> // !!!!!!!!!!!!!!!!!!!!!!!!!
#include <assert.h> // !!!!!!!!!!!!!!!!!!!!!!!!!
#include <string.h>
#include "callback.h"
#include "fail.h"
#include "memory.h"
#include "alloc.h"
#include "mlvalues.h"

#ifndef NATIVE_CODE

/* Bytecode callbacks */

#include "interp.h"
#include "instruct.h"
#include "fix_code.h"
#include "stacks.h"

#ifndef LOCAL_CALLBACK_BYTECODE
static opcode_t callback_code[] = { ACC, 0, APPLY, 0, POP, 1, STOP };
#endif


#ifdef THREADED_CODE

static void thread_callback_r(CAML_R)
{
  caml_thread_code_r(ctx, callback_code, sizeof(callback_code));
  callback_code_threaded = 1;
}

#define Init_callback() if (!callback_code_threaded) thread_callback_r(ctx)

#else

#define Init_callback()

#endif

CAMLexport value caml_callbackN_exn_r(CAML_R, value closure, int narg, value args[])
{
  int i;
  value res;

  /* some alternate bytecode implementations (e.g. a JIT translator)
     might require that the bytecode is kept in a local variable on
     the C stack */
#ifdef LOCAL_CALLBACK_BYTECODE
  opcode_t local_callback_code[7];
#endif

  Assert(narg + 4 <= 256);

  caml_extern_sp -= narg + 4;
  for (i = 0; i < narg; i++) caml_extern_sp[i] = args[i]; /* arguments */
#ifndef LOCAL_CALLBACK_BYTECODE
  caml_extern_sp[narg] = (value) (callback_code + 4); /* return address */
  caml_extern_sp[narg + 1] = Val_unit;    /* environment */
  caml_extern_sp[narg + 2] = Val_long(0); /* extra args */
  caml_extern_sp[narg + 3] = closure;
  Init_callback();
  callback_code[1] = narg + 3;
  callback_code[3] = narg;
  res = caml_interprete_r(ctx, callback_code, sizeof(callback_code));
#else /*have LOCAL_CALLBACK_BYTECODE*/
  caml_extern_sp[narg] = (value) (local_callback_code + 4); /* return address */
  caml_extern_sp[narg + 1] = Val_unit;    /* environment */
  caml_extern_sp[narg + 2] = Val_long(0); /* extra args */
  caml_extern_sp[narg + 3] = closure;
  local_callback_code[0] = ACC;
  local_callback_code[1] = narg + 3;
  local_callback_code[2] = APPLY;
  local_callback_code[3] = narg;
  local_callback_code[4] = POP;
  local_callback_code[5] =  1;
  local_callback_code[6] = STOP;
#ifdef THREADED_CODE
  // FIXME: this hasn't been replaced with an "_r" version; is it intentional? --Luca Saiu REENTRANTRUNTIME
  caml_thread_code(local_callback_code, sizeof(local_callback_code));
#endif /*THREADED_CODE*/
  res = caml_interprete(local_callback_code, sizeof(local_callback_code));
  caml_release_bytecode(local_callback_code, sizeof(local_callback_code));
#endif /*LOCAL_CALLBACK_BYTECODE*/
  if (Is_exception_result(res)) caml_extern_sp += narg + 4; /* PR#1228 */
  return res;
}

CAMLexport value caml_callback_exn_r(CAML_R, value closure, value arg1)
{
  value arg[1];
  arg[0] = arg1;
  return caml_callbackN_exn_r(ctx, closure, 1, arg);
}

CAMLexport value caml_callback2_exn_r(CAML_R, value closure, value arg1, value arg2)
{
  value arg[2];
  arg[0] = arg1;
  arg[1] = arg2;
  return caml_callbackN_exn_r(ctx, closure, 2, arg);
}

CAMLexport value caml_callback3_exn_r(CAML_R, value closure,
                               value arg1, value arg2, value arg3)
{
  value arg[3];
  arg[0] = arg1;
  arg[1] = arg2;
  arg[2] = arg3;
  return caml_callbackN_exn_r(ctx, closure, 3, arg);
}

#else

/* Native-code callbacks.  caml_callback[123]_exn are implemented in asm. */

CAMLexport value caml_callbackN_exn_r(CAML_R, value closure, int narg, value args[])
{
  CAMLparam1 (closure);
  CAMLxparamN (args, narg);
  CAMLlocal1 (res);
  int i;

  res = closure;
  for (i = 0; i < narg; /*nothing*/) {
    /* Pass as many arguments as possible */
    switch (narg - i) {
    case 1:
      res = caml_callback_exn_r(ctx, res, args[i]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 1;
      break;
    case 2:
      res = caml_callback2_exn_r(ctx, res, args[i], args[i + 1]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 2;
      break;
    default:
      res = caml_callback3_exn_r(ctx, res, args[i], args[i + 1], args[i + 2]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 3;
      break;
    }
  }
  CAMLreturn (res);
}

#endif

/* Exception-propagating variants of the above */

CAMLexport value caml_callback_r (CAML_R, value closure, value arg)
{
  value res = caml_callback_exn_r(ctx, closure, arg);
  if (Is_exception_result(res)) caml_raise_r(ctx, Extract_exception(res));
  return res;
}

CAMLexport value caml_callback2_r (CAML_R, value closure, value arg1, value arg2)
{
  value res = caml_callback2_exn_r(ctx, closure, arg1, arg2);
  if (Is_exception_result(res)) caml_raise_r(ctx, Extract_exception(res));
  return res;
}

CAMLexport value caml_callback3_r (CAML_R, value closure, value arg1, value arg2,
                                   value arg3)
{
  value res = caml_callback3_exn_r(ctx, closure, arg1, arg2, arg3);
  if (Is_exception_result(res)) caml_raise_r(ctx, Extract_exception(res));
  return res;
}

CAMLexport value caml_callbackN_r (CAML_R, value closure, int narg, value args[])
{
  value res = caml_callbackN_exn_r(ctx, closure, narg, args);
  if (Is_exception_result(res)) caml_raise_r(ctx, Extract_exception(res));
  return res;
}

static unsigned int hash_value_name(char const *name)
{
  unsigned int h;
  for (h = 0; *name != 0; name++) h = h * 19 + *name;
  return h % Named_value_size;
}

CAMLprim value caml_register_named_value_r(CAML_R, value vname, value val)
{
  struct named_value * nv;
  char * name = String_val(vname);
  unsigned int h = hash_value_name(name);

  for (nv = named_value_table[h]; nv != NULL; nv = nv->next) {
    if (strcmp(name, nv->name) == 0) {
      nv->val = val;
      return Val_unit;
    }
  }
  nv = (struct named_value *)
         caml_stat_alloc(sizeof(struct named_value) + strlen(name));
  strcpy(nv->name, name);
  nv->val = val;
  nv->next = named_value_table[h];
  named_value_table[h] = nv;
  caml_register_global_root_r(ctx, &nv->val);
  return Val_unit;
}

CAMLexport value * caml_named_value_r(CAML_R, char const *name)
{
  struct named_value * nv;
  for (nv = named_value_table[hash_value_name(name)];
       nv != NULL;
       nv = nv->next) {
    if (strcmp(name, nv->name) == 0) return &nv->val;
  }
  return NULL;
}

/* Helper function for caml_named_value_table_as_caml_value_r */
static value caml_named_value_table_bucket_as_caml_value_r(CAML_R, struct named_value *bucket){
  CAMLparam0();
  CAMLlocal1(result);
  result = Val_emptylist;

  if(bucket != NULL){
    result = caml_alloc_r(ctx, 3, 0);
    caml_modify_r(ctx, &Field(result, 0), caml_copy_string_r(ctx, bucket->name));
    caml_modify_r(ctx, &Field(result, 1), bucket->val);
    caml_modify_r(ctx, &Field(result, 2), caml_named_value_table_bucket_as_caml_value_r(ctx, bucket->next));
  }
  CAMLreturn(result);
}

CAMLexport value caml_named_value_table_as_caml_value_r(CAML_R){
  CAMLparam0();
  CAMLlocal3(result, bucket, bucket_item);
  int i;
  result = caml_alloc_r(ctx, Named_value_size, 0);
  for(i = 0; i < Named_value_size; i ++){
    bucket = caml_named_value_table_bucket_as_caml_value_r(ctx, named_value_table[i]);
    caml_modify_r(ctx, &Field(result, i), bucket);
  }
  DUMP("result is %p", (void*)(long)result);
  CAMLreturn(result);
}

/* Helper function for caml_install_named_value_table_as_caml_value_r */
static struct named_value* caml_named_value_table_bucket_from_caml_value_r(CAML_R, value caml_bucket){
  struct named_value *result = NULL;
  CAMLparam0();
  CAMLlocal1(caml_name);
  result = NULL;
  if(caml_bucket != Val_emptylist){
    caml_name = Field(caml_bucket, 0);
    result = caml_stat_alloc(sizeof(struct named_value) + caml_string_length(caml_name));
    result->val = Field(caml_bucket, 1);
    caml_register_global_root_r(ctx, &result->val);
    strcpy(result->name, String_val(caml_name));
    result->next = caml_named_value_table_bucket_from_caml_value_r(ctx, Field(caml_bucket, 2));
  }
  CAMLreturnT(struct named_value*, result);
}

CAMLexport void caml_install_named_value_table_as_caml_value_r(CAML_R, value encoded_named_value_table){
  CAMLparam1(encoded_named_value_table);
  int i;
  for(i = 0; i < Named_value_size; i ++)
    named_value_table[i] =
      caml_named_value_table_bucket_from_caml_value_r(ctx, Field(encoded_named_value_table, i));
  CAMLreturn0;
}

/* Helper for caml_destroy_named_value_table_r */
static void caml_destroy_named_value_table_bucket_r(CAML_R, struct named_value *bucket){
  //DUMP("Destroying the bucket at %p", bucket);
  while(bucket != NULL){
    void *next = bucket->next;
    //DUMP("Destroying the bbucket at %p", bucket);
    free(bucket);
    bucket = next;
    //DUMP("The bucket rest is now %p", bucket);
  }
}
CAMLexport void caml_destroy_named_value_table_r(CAML_R){
  int i;
  for(i = 0; i < Named_value_size; i ++){
    //DUMP("Destroying the %i-th bucket", i);
    caml_destroy_named_value_table_bucket_r(ctx, named_value_table[i]);
  }
}
