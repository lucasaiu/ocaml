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

#include "socketaddr.h"

CAMLprim value unix_bind_r(CAML_R, value socket, value address)
{
  int ret;
  union sock_addr_union addr;
  socklen_param_type addr_len;

  get_sockaddr_r(ctx, address, &addr, &addr_len);
  ret = bind(Int_val(socket), &addr.s_gen, addr_len);
  if (ret == -1) uerror_r(ctx, "bind", Nothing);
  return Val_unit;
}

#else

CAMLprim value unix_bind_r(CAML_R, value socket, value address)
{ caml_invalid_argument_r(ctx, "bind not implemented"); }

#endif
