/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2004 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#define CAML_CONTEXT_ROOTS

#include <string.h>
#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>
#include <memory.h>
#include <signals.h>
#include "unixsupport.h"

#if defined(HAS_SOCKETS) && defined(HAS_IPV6)

#include "socketaddr.h"
#ifndef _WIN32
#include <sys/types.h>
#include <netdb.h>
#endif

static int getnameinfo_flag_table[] = {
  NI_NOFQDN, NI_NUMERICHOST, NI_NAMEREQD, NI_NUMERICSERV, NI_DGRAM
};

CAMLprim value unix_getnameinfo_r(CAML_R, value vaddr, value vopts)
{
  CAMLparam0();
  CAMLlocal3(vhost, vserv, vres);
  union sock_addr_union addr;
  socklen_param_type addr_len;
  char host[4096];
  char serv[1024];
  int opts, retcode;

  get_sockaddr_r(ctx, vaddr, &addr, &addr_len);
  opts = convert_flag_list(vopts, getnameinfo_flag_table);
  caml_enter_blocking_section_r(ctx);
  retcode =
    getnameinfo((const struct sockaddr *) &addr.s_gen, addr_len,
                host, sizeof(host), serv, sizeof(serv), opts);
  caml_leave_blocking_section_r(ctx);
  if (retcode != 0) caml_raise_not_found_r(ctx); /* TODO: detailed error reporting? */
  vhost = caml_copy_string_r(ctx, host);
  vserv = caml_copy_string_r(ctx, serv);
  vres = caml_alloc_small_r(ctx, 2, 0);
  Field(vres, 0) = vhost;
  Field(vres, 1) = vserv;
  CAMLreturn(vres);
}

#else

CAMLprim value unix_getnameinfo_r(CAML_R, value vaddr, value vopts)
{ caml_invalid_argument_r(ctx, "getnameinfo not implemented"); }

#endif
