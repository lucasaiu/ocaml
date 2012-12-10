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

#define CAML_CONTEXT_ROOTS

#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>
#include <memory.h>
#include <signals.h>
#include "unixsupport.h"

#ifdef HAS_SOCKETS

#include "socketaddr.h"

CAMLprim value unix_accept_r(CAML_R, value sock)
{
  int retcode;
  value res;
  value a;
  union sock_addr_union addr;
  socklen_param_type addr_len;

  addr_len = sizeof(addr);
  caml_enter_blocking_section_r(ctx);
  retcode = accept(Int_val(sock), &addr.s_gen, &addr_len);
  caml_leave_blocking_section_r(ctx);
  if (retcode == -1) uerror_r(ctx, "accept", Nothing);
  a = alloc_sockaddr_r(ctx, &addr, addr_len, retcode);
  Begin_root (a);
  res = caml_alloc_small_r(ctx, 2, 0);
    Field(res, 0) = Val_int(retcode);
    Field(res, 1) = a;
  End_roots();
  return res;
}

#else

CAMLprim value unix_accept_r(CAML_R, value sock)
{ caml_invalid_argument_r(ctx, "accept not implemented"); }

#endif
