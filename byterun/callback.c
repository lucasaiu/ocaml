/***********************************************************************/
/*                                                                     */
/*                           Objective Caml                            */
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

/* Callbacks from C to Caml */

#include <string.h>
#include "callback.h"
#include "fail.h"
#include "memory.h"
#include "mlvalues.h"

#ifndef NATIVE_CODE

/* Bytecode callbacks */

#include "interp.h"
#include "instruct.h"
#include "fix_code.h"
#include "stacks.h"

int caml_callback_depth = 0;

static opcode_t callback_code[] = { ACC, 0, APPLY, 0, POP, 1, STOP };

#ifdef THREADED_CODE

static int callback_code_threaded = 0;

static void thread_callback(void)
{
  caml_thread_code(callback_code, sizeof(callback_code));
  callback_code_threaded = 1;
}

#define Init_callback() if (!callback_code_threaded) thread_callback()

#else

#define Init_callback()

#endif

CAMLexport value caml_callbackN_exn(value closure, int narg, value args[])
{
  int i;
  value res;

  Assert(narg + 4 <= 256);
  Init_callback();
  caml_extern_sp -= narg + 4;
  for (i = 0; i < narg; i++) caml_extern_sp[i] = args[i]; /* arguments */
  caml_extern_sp[narg] = (value) (callback_code + 4); /* return address */
  caml_extern_sp[narg + 1] = Val_unit;    /* environment */
  caml_extern_sp[narg + 2] = Val_long(0); /* extra args */
  caml_extern_sp[narg + 3] = closure;
  callback_code[1] = narg + 3;
  callback_code[3] = narg;
  res = caml_interprete(callback_code, sizeof(callback_code));
  if (Is_exception_result(res)) caml_extern_sp += narg + 4; /* PR#1228 */
  return res;
}

CAMLexport value caml_callback_exn(value closure, value arg1)
{
  value arg[1];
  arg[0] = arg1;
  return caml_callbackN_exn(closure, 1, arg);
}

CAMLexport value caml_callback2_exn(value closure, value arg1, value arg2)
{
  value arg[2];
  arg[0] = arg1;
  arg[1] = arg2;
  return caml_callbackN_exn(closure, 2, arg);
}

CAMLexport value caml_callback3_exn(value closure,
                               value arg1, value arg2, value arg3)
{
  value arg[3];
  arg[0] = arg1;
  arg[1] = arg2;
  arg[2] = arg3;
  return caml_callbackN_exn(closure, 3, arg);
}

#else

/* Native-code callbacks.  caml_callback[123]_exn are implemented in asm. */

CAMLexport value caml_callbackN_exn(value closure, int narg, value args[])
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
      res = caml_callback_exn(res, args[i]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 1;
      break;
    case 2:
      res = caml_callback2_exn(res, args[i], args[i + 1]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 2;
      break;
    default:
      res = caml_callback3_exn(res, args[i], args[i + 1], args[i + 2]);
      if (Is_exception_result(res)) CAMLreturn (res);
      i += 3;
      break;
    }
  }
  CAMLreturn (res);
}

#endif

/* Exception-propagating variants of the above */

CAMLexport value caml_callback (value closure, value arg)
{
  value res = caml_callback_exn(closure, arg);
  if (Is_exception_result(res)) caml_raise(Extract_exception(res));
  return res;
}

CAMLexport value caml_callback2 (value closure, value arg1, value arg2)
{
  value res = caml_callback2_exn(closure, arg1, arg2);
  if (Is_exception_result(res)) caml_raise(Extract_exception(res));
  return res;
}

CAMLexport value caml_callback3 (value closure, value arg1, value arg2,
                                 value arg3)
{
  value res = caml_callback3_exn(closure, arg1, arg2, arg3);
  if (Is_exception_result(res)) caml_raise(Extract_exception(res));
  return res;
}

CAMLexport value caml_callbackN (value closure, int narg, value args[])
{
  value res = caml_callbackN_exn(closure, narg, args);
  if (Is_exception_result(res)) caml_raise(Extract_exception(res));
  return res;
}

/* Naming of Caml values */

struct named_value {
  value val;
  struct named_value * next;
  char name[1];
};

#define Named_value_size 13

static struct named_value * named_value_table[Named_value_size] = { NULL, };

static unsigned int hash_value_name(char *name)
{
  unsigned int h;
  for (h = 0; *name != 0; name++) h = h * 19 + *name;
  return h % Named_value_size;
}

CAMLprim value caml_register_named_value(value vname, value val)
{
  struct named_value * nv;
  char * name = String_val(vname);
  unsigned int h = hash_value_name(name);

  nv = (struct named_value *)
         caml_stat_alloc(sizeof(struct named_value) + strlen(name));
  strcpy(nv->name, name);
  nv->val = val;
  nv->next = named_value_table[h];
  named_value_table[h] = nv;
  caml_register_global_root(&nv->val);
  return Val_unit;
}

CAMLexport value * caml_named_value(char *name)
{
  struct named_value * nv;
  for (nv = named_value_table[hash_value_name(name)];
       nv != NULL;
       nv = nv->next) {
    if (strcmp(name, nv->name) == 0) return &nv->val;
  }
  return NULL;
}
