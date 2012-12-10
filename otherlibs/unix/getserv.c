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
#include "unixsupport.h"

#ifdef HAS_SOCKETS

#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

static value alloc_service_entry_r(CAML_R, struct servent *entry)
{
  value res;
  value name = Val_unit, aliases = Val_unit, proto = Val_unit;

  Begin_roots3 (name, aliases, proto);
    name = caml_copy_string_r(ctx,entry->s_name);
    aliases = caml_copy_string_array_r(ctx,(const char**)entry->s_aliases);
    proto = caml_copy_string_r(ctx,entry->s_proto);
    res = caml_alloc_small_r(ctx,4, 0);
    Field(res,0) = name;
    Field(res,1) = aliases;
    Field(res,2) = Val_int(ntohs(entry->s_port));
    Field(res,3) = proto;
  End_roots();
  return res;
}

CAMLprim value unix_getservbyname_r(CAML_R, value name, value proto)
{
  struct servent * entry;
  entry = getservbyname(String_val(name), String_val(proto));
  if (entry == (struct servent *) NULL) caml_raise_not_found_r(ctx);
  return alloc_service_entry_r(ctx, entry);
}

CAMLprim value unix_getservbyport_r(CAML_R, value port, value proto)
{
  struct servent * entry;
  entry = getservbyport(htons(Int_val(port)), String_val(proto));
  if (entry == (struct servent *) NULL) caml_raise_not_found_r(ctx);
  return alloc_service_entry_r(ctx, entry);
}

#else

CAMLprim value unix_getservbyport_r(CAML_R, value port, value proto)
{ caml_invalid_argument_r(ctx,"getservbyport not implemented"); }

CAMLprim value unix_getservbyname_r(CAML_R, value name, value proto)
{ caml_invalid_argument_r(ctx,"getservbyname not implemented"); }

#endif
