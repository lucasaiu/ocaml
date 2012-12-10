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

#define CAML_CONTEXT_STARTUP

#include <mlvalues.h>
#include <alloc.h>

#ifndef _WIN32
extern char ** environ;
#endif

CAMLprim value unix_environment_r(CAML_R, value unit)
{
  if (environ != NULL) {
    return caml_copy_string_array_r(ctx, (const char**)environ);
  } else {
    return Atom(0);
  }
}
