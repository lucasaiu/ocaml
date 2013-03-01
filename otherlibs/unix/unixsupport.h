/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#include "context.h"

#define Nothing ((value) 0)

extern value unix_error_of_code_r (CAML_R, int errcode);
extern void unix_error_r (CAML_R, int errcode, char * cmdname, value arg) Noreturn;
extern void uerror_r (CAML_R, char * cmdname, value arg) Noreturn;

#define UNIX_BUFFER_SIZE 65536

#define DIR_Val(v) *((DIR **) &Field(v, 0))

typedef struct caml_unix_context {
  value * unix_error_exn;
} caml_unix_context;

extern caml_unix_context *caml_get_unix_context_r(CAML_R);
