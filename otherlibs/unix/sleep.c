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
#include <signals.h>
#include "unixsupport.h"

CAMLprim value unix_sleep_r(CAML_R, value t)
{
  caml_enter_blocking_section_r(ctx);
  sleep(Int_val(t));
  caml_leave_blocking_section_r(ctx);
  return Val_unit;
}
