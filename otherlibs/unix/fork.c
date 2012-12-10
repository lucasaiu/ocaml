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

#define CAML_CONTEXT_DEBUGGER

#include <mlvalues.h>
#include <debugger.h>
#include "unixsupport.h"

CAMLprim value unix_fork_r(CAML_R, value unit)
{
  int ret;
  ret = fork();
  if (ret == -1) uerror_r(ctx,"fork", Nothing);
  if (caml_debugger_in_use)
    if ((caml_debugger_fork_mode && ret == 0) ||
        (!caml_debugger_fork_mode && ret != 0))
      caml_debugger_cleanup_fork_r(ctx);
  return Val_int(ret);
}
