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

#ifdef HAS_SOCKETS

#include <sys/socket.h>

CAMLprim value unix_listen_r(CAML_R, value sock, value backlog)
{
  if (listen(Int_val(sock), Int_val(backlog)) == -1) uerror_r(ctx,"listen", Nothing);
  return Val_unit;
}

#else

CAMLprim value unix_listen_r(CAML_R, value sock, value backlog)
{ caml_invalid_argument_r(ctx,"listen not implemented"); }

#endif
