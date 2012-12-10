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
#ifndef _WIN32
#include <sys/param.h>
#endif
#include "unixsupport.h"

#ifdef HAS_GETHOSTNAME

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

CAMLprim value unix_gethostname_r(CAML_R, value unit)
{
  char name[MAXHOSTNAMELEN];
  gethostname(name, MAXHOSTNAMELEN);
  name[MAXHOSTNAMELEN-1] = 0;
  return caml_copy_string_r(ctx,name);
}

#else
#ifdef HAS_UNAME

#include <sys/utsname.h>

CAMLprim value unix_gethostname_r(CAML_R, value unit)
{
  struct utsname un;
  uname(&un);
  return caml_copy_string_r(ctx,un.nodename);
}

#else

CAMLprim value unix_gethostname_r(CAML_R, value unit)
{ caml_invalid_argument_r(ctx,"gethostname not implemented"); }

#endif
#endif
