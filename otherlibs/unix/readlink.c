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

#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>

#ifdef HAS_SYMLINK

#include <sys/param.h>
#include "unixsupport.h"

#ifndef PATH_MAX
#ifdef MAXPATHLEN
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 512
#endif
#endif

CAMLprim value unix_readlink_r(CAML_R, value path)
{
  char buffer[PATH_MAX];
  int len;
  len = readlink(String_val(path), buffer, sizeof(buffer) - 1);
  if (len == -1) uerror_r(ctx,"readlink", path);
  buffer[len] = '\0';
  return caml_copy_string_r(ctx,buffer);
}

#else

CAMLprim value unix_readlink_r(CAML_R, value path)
{ caml_invalid_argument_r(ctx,"readlink not implemented"); }

#endif
