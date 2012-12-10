/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*  Contributed by Stephane Glondu <steph@glondu.net>                  */
/*                                                                     */
/*  Copyright 2009 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>

#ifdef HAS_INITGROUPS

#include <sys/types.h>
#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#include <limits.h>
#include <grp.h>
#include "unixsupport.h"

CAMLprim value unix_initgroups_r(CAML_R, value user, value group)
{
  if (initgroups(String_val(user), Int_val(group)) == -1) {
    uerror_r(ctx,"initgroups", Nothing);
  }
  return Val_unit;
}

#else

CAMLprim value unix_initgroups_r(CAML_R, value user, value group)
{ caml_invalid_argument_r(ctx,"initgroups not implemented"); }

#endif
