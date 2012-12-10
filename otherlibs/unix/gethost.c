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
#define CAML_CONTEXT_STARTUP

#include <string.h>
#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>
#include <memory.h>
#include <signals.h>
#include "unixsupport.h"

#ifdef HAS_SOCKETS

#include "socketaddr.h"
#ifndef _WIN32
#include <sys/types.h>
#include <netdb.h>
#endif

#define NETDB_BUFFER_SIZE 10000

#ifdef _WIN32
#define GETHOSTBYADDR_IS_REENTRANT 1
#define GETHOSTBYNAME_IS_REENTRANT 1
#endif

static int entry_h_length;

extern int socket_domain_table[];

static value alloc_one_addr_r(CAML_R, char const *a)
{
  struct in_addr addr;
#ifdef HAS_IPV6
  struct in6_addr addr6;
  if (entry_h_length == 16) {
    memmove(&addr6, a, 16);
    return alloc_inet6_addr_r(ctx, &addr6);
  }
#endif
  memmove (&addr, a, 4);
  return alloc_inet_addr_r(ctx, &addr);
}

static value alloc_host_entry_r(CAML_R, struct hostent *entry)
{
  value res;
  value name = Val_unit, aliases = Val_unit;
  value addr_list = Val_unit, adr = Val_unit;

  Begin_roots4 (name, aliases, addr_list, adr);
  name = caml_copy_string_r(ctx, (char *)(entry->h_name));
    /* PR#4043: protect against buggy implementations of gethostbyname()
       that return a NULL pointer in h_aliases */
    if (entry->h_aliases)
      aliases = caml_copy_string_array_r(ctx, (const char**)entry->h_aliases);
    else
      aliases = Atom(0);
    entry_h_length = entry->h_length;
#ifdef h_addr
    addr_list = caml_alloc_array_r(ctx, alloc_one_addr_r, (const char**)entry->h_addr_list);
#else
    adr = alloc_one_addr(entry->h_addr);
    addr_list = caml_alloc_small_r(ctx,1, 0);
    Field(addr_list, 0) = adr;
#endif
    res = caml_alloc_small_r(ctx, 4, 0);
    Field(res, 0) = name;
    Field(res, 1) = aliases;
    switch (entry->h_addrtype) {
    case PF_UNIX:          Field(res, 2) = Val_int(0); break;
    case PF_INET:          Field(res, 2) = Val_int(1); break;
    default: /*PF_INET6 */ Field(res, 2) = Val_int(2); break;
    }
    Field(res, 3) = addr_list;
  End_roots();
  return res;
}

CAMLprim value unix_gethostbyaddr_r(CAML_R, value a)
{
  struct in_addr adr = GET_INET_ADDR(a);
  struct hostent * hp;
#if HAS_GETHOSTBYADDR_R == 7
  struct hostent h;
  char buffer[NETDB_BUFFER_SIZE];
  int h_errnop;
  caml_enter_blocking_section_r(ctx);
  hp = gethostbyaddr_r((char *) &adr, 4, AF_INET,
                       &h, buffer, sizeof(buffer), &h_errnop);
  caml_leave_blocking_section_r(ctx);
#elif HAS_GETHOSTBYADDR_R == 8
  struct hostent h;
  char buffer[NETDB_BUFFER_SIZE];
  int h_errnop, rc;
  caml_enter_blocking_section_r(ctx);
  rc = gethostbyaddr_r((char *) &adr, 4, AF_INET,
                       &h, buffer, sizeof(buffer), &hp, &h_errnop);
  caml_leave_blocking_section_r(ctx);
  if (rc != 0) hp = NULL;
#else
#ifdef GETHOSTBYADDR_IS_REENTRANT
  caml_enter_blocking_section_r(ctx);
#endif
  hp = gethostbyaddr((char *) &adr, 4, AF_INET);
#ifdef GETHOSTBYADDR_IS_REENTRANT
  caml_leave_blocking_section_r(ctx);
#endif
#endif
  if (hp == (struct hostent *) NULL) caml_raise_not_found_r(ctx);
  return alloc_host_entry_r(ctx, hp);
}

CAMLprim value unix_gethostbyname_r(CAML_R, value name)
{
  struct hostent * hp;
  char * hostname;

#if HAS_GETHOSTBYNAME_R || GETHOSTBYNAME_IS_REENTRANT
  hostname = stat_alloc(string_length(name) + 1);
  strcpy(hostname, String_val(name));
#else
  hostname = String_val(name);
#endif

#if HAS_GETHOSTBYNAME_R == 5
  {
    struct hostent h;
    char buffer[NETDB_BUFFER_SIZE];
    int h_errno;
    caml_enter_blocking_section_r(ctx);
    hp = gethostbyname_r(hostname, &h, buffer, sizeof(buffer), &h_errno);
    caml_leave_blocking_section_r(ctx);
  }
#elif HAS_GETHOSTBYNAME_R == 6
  {
    struct hostent h;
    char buffer[NETDB_BUFFER_SIZE];
    int h_errno, rc;
    caml_enter_blocking_section_r(ctx);
    rc = gethostbyname_r(hostname, &h, buffer, sizeof(buffer), &hp, &h_errno);
    caml_leave_blocking_section_r(ctx);
    if (rc != 0) hp = NULL;
  }
#else
#ifdef GETHOSTBYNAME_IS_REENTRANT
  caml_enter_blocking_section_r(ctx);
#endif
  hp = gethostbyname(hostname);
#ifdef GETHOSTBYNAME_IS_REENTRANT
  caml_leave_blocking_section_r(ctx);
#endif
#endif

#if HAS_GETHOSTBYNAME_R || GETHOSTBYNAME_IS_REENTRANT
  stat_free(hostname);
#endif

  if (hp == (struct hostent *) NULL) caml_raise_not_found_r(ctx);
  return alloc_host_entry_r(ctx, hp);
}

#else

CAMLprim value unix_gethostbyaddr_r(CAML_R, value name)
{ caml_invalid_argument_r(ctx, "gethostbyaddr not implemented"); }

CAMLprim value unix_gethostbyname_r(CAML_R, value name)
{ caml_invalid_argument_r(ctx, "gethostbyname not implemented"); }

#endif
