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

CAMLextern value caml_callback (dont_use, value closure, value arg);
CAMLextern value caml_callback2 (dont_use, value closure, value arg1, value arg2);
CAMLextern value caml_callback3 (dont_use, value closure, value arg1, value arg2,
                                 value arg3);
CAMLextern value caml_callbackN (dont_use, value closure, int narg, value args[]);

CAMLextern value caml_callback_exn (dont_use, value closure, value arg);
CAMLextern value caml_callback2_exn (dont_use, value closure, value arg1, value arg2);
CAMLextern value caml_callback3_exn (dont_use, value closure,
                                     value arg1, value arg2, value arg3);
CAMLextern value caml_callbackN_exn (dont_use, value closure, int narg, value args[]);

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

/* CAMLextern int caml_callback_depth; */

#ifdef __cplusplus
}
#endif

#endif
