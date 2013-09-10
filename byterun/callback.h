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

#ifndef CAML_CALLBACK_H
#define CAML_CALLBACK_H

#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "mlvalues.h"
#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CAMLextern value caml_callback (dont_use, value closure, value arg); */
/* CAMLextern value caml_callback2 (dont_use, value closure, value arg1, value arg2); */
/* CAMLextern value caml_callback3 (dont_use, value closure, value arg1, value arg2, */
/*                                  value arg3); */
/* CAMLextern value caml_callbackN (dont_use, value closure, int narg, value args[]); */

#if defined(NATIVE_CODE) && !defined(SUPPORTS_MULTICONTEXT)
CAMLextern value caml_callback (value closure, value arg);
CAMLextern value caml_callback2 (value closure, value arg1, value arg2);
CAMLextern value caml_callback3 (value closure, value arg1, value arg2,
                                 value arg3);
CAMLextern value caml_callbackN (value closure, int narg, value args[]);
#endif // #if defined(NATIVE_CODE) && !defined(SUPPORTS_MULTICONTEXT)

/* CAMLextern value caml_callback_exn (dont_use, value closure, value arg); */
/* CAMLextern value caml_callback2_exn (dont_use, value closure, value arg1, value arg2); */
/* CAMLextern value caml_callback3_exn (dont_use, value closure, */
/*                                      value arg1, value arg2, value arg3); */
/* CAMLextern value caml_callbackN_exn (dont_use, value closure, int narg, value args[]); */

#if defined(NATIVE_CODE) && !defined(SUPPORTS_MULTICONTEXT)
CAMLextern value caml_callback_exn (value closure, value arg);
CAMLextern value caml_callback2_exn (value closure, value arg1, value arg2);
CAMLextern value caml_callback3_exn (value closure,
                                     value arg1, value arg2, value arg3);
CAMLextern value caml_callbackN_exn (value closure, int narg, value args[]);
#endif // #if defined(NATIVE_CODE) && !defined(SUPPORTS_MULTICONTEXT)

CAMLextern value caml_callback_r (CAML_R, value closure, value arg);
CAMLextern value caml_callback2_r (CAML_R, value closure, value arg1, value arg2);
CAMLextern value caml_callback3_r (CAML_R, value closure, value arg1, value arg2,
                                 value arg3);
CAMLextern value caml_callbackN_r (CAML_R, value closure, int narg, value args[]);

CAMLextern value caml_callback_exn_r (CAML_R, value closure, value arg);
CAMLextern value caml_callback2_exn_r (CAML_R, value closure, value arg1, value arg2);
CAMLextern value caml_callback3_exn_r (CAML_R, value closure,
                                     value arg1, value arg2, value arg3);
CAMLextern value caml_callbackN_exn_r (CAML_R, value closure, int narg, value args[]);

#define Make_exception_result(v) ((v) | 2)
#define Is_exception_result(v) (((v) & 3) == 2)
#define Extract_exception(v) ((v) & ~3)

CAMLextern value * caml_named_value_r (CAML_R, char const * name);

CAMLextern caml_global_context * caml_main_rr (char ** argv);
CAMLextern void caml_startup (char ** argv);

/* Return a Caml encoding of the current named_value_table.  This is
   needed to copy the table at split time, sharing correctly. */
CAMLextern value caml_named_value_table_as_caml_value_r(CAML_R);

/* Given a Caml encoding of named_value_table, install it in the given
   context, setting up roots as needed.  The contained Caml values
   have to refer the given context heap, so this is intended to be
   used on an encoding obtained from deserializing a blob. */
CAMLextern void caml_install_named_value_table_as_caml_value_r(CAML_R, value encoded_named_value_table);

/* Destroy dynamically-allocated structures */
CAMLextern void caml_destroy_named_value_table_r(CAML_R);

/* CAMLextern int caml_callback_depth; */

#ifdef __cplusplus
}
#endif

#endif
