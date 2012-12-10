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
#include "unixsupport.h"

#if !defined (_WIN32) && !macintosh
#include <sys/param.h>
#endif

#ifndef PATH_MAX
#ifdef MAXPATHLEN
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 512
#endif
#endif

#ifdef HAS_GETCWD

CAMLprim value unix_getcwd_r(CAML_R, value unit)
{
  char buff[PATH_MAX];
  if (getcwd(buff, sizeof(buff)) == 0) uerror_r(ctx,"getcwd", Nothing);
  return caml_copy_string_r(ctx, buff);
}

#else
#ifdef HAS_GETWD

CAMLprim value unix_getcwd_r(CAML_R, value unit)
{
  char buff[PATH_MAX];
  if (getwd(buff) == 0) uerror_r(ctx,"getcwd", caml_copy_string_r(ctx,buff));
  return caml_copy_string_r(ctx,buff);
}

#else

CAMLprim value unix_getcwd_r(CAML_R, value unit)
{ caml_invalid_argument_r(ctx,"getcwd not implemented"); }

#endif
#endif
