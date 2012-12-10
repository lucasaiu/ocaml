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

#include <fail.h>
#include <mlvalues.h>
#include "unixsupport.h"
#include <errno.h>
#include <sys/types.h>
#ifdef HAS_DIRENT
#include <dirent.h>
#else
#include <sys/dir.h>
#endif

#ifdef HAS_REWINDDIR

CAMLprim value unix_rewinddir_r(CAML_R, value vd)
{
  DIR * d = DIR_Val(vd);
  if (d == (DIR *) NULL) unix_error_r(ctx,EBADF, "rewinddir", Nothing);
  rewinddir(d);
  return Val_unit;
}

#else

CAMLprim value unix_rewinddir_r(CAML_R, value d)
{ caml_invalid_argument_r(ctx,"rewinddir not implemented"); }

#endif
