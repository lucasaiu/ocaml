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
#include <memory.h>
#include <fail.h>
#include "unixsupport.h"
#include <pwd.h>

static value alloc_passwd_entry_r(CAML_R, struct passwd *entry)
{
  value res;
  value name = Val_unit, passwd = Val_unit, gecos = Val_unit;
  value dir = Val_unit, shell = Val_unit;

  Begin_roots5 (name, passwd, gecos, dir, shell);
    name = caml_copy_string_r(ctx,entry->pw_name);
    passwd = caml_copy_string_r(ctx,entry->pw_passwd);
#ifndef __BEOS__
    gecos = caml_copy_string_r(ctx,entry->pw_gecos);
#else
    gecos = caml_copy_string_r(ctx,"");
#endif
    dir = caml_copy_string_r(ctx,entry->pw_dir);
    shell = caml_copy_string_r(ctx,entry->pw_shell);
    res = caml_alloc_small_r(ctx,7, 0);
    Field(res,0) = name;
    Field(res,1) = passwd;
    Field(res,2) = Val_int(entry->pw_uid);
    Field(res,3) = Val_int(entry->pw_gid);
    Field(res,4) = gecos;
    Field(res,5) = dir;
    Field(res,6) = shell;
  End_roots();
  return res;
}

CAMLprim value unix_getpwnam_r(CAML_R, value name)
{
  struct passwd * entry;
  entry = getpwnam(String_val(name));
  if (entry == (struct passwd *) NULL) caml_raise_not_found_r(ctx);
  return alloc_passwd_entry_r(ctx, entry);
}

CAMLprim value unix_getpwuid_r(CAML_R, value uid)
{
  struct passwd * entry;
  entry = getpwuid(Int_val(uid));
  if (entry == (struct passwd *) NULL) caml_raise_not_found_r(ctx);
  return alloc_passwd_entry_r(ctx, entry);
}
