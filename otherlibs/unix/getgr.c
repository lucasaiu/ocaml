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
#include <fail.h>
#include <alloc.h>
#include <memory.h>
#include "unixsupport.h"
#include <stdio.h>
#include <grp.h>

static value alloc_group_entry_r(CAML_R, struct group *entry)
{
  value res;
  value name = Val_unit, pass = Val_unit, mem = Val_unit;

  Begin_roots3 (name, pass, mem);
  name = caml_copy_string_r(ctx, entry->gr_name);
  pass = caml_copy_string_r(ctx, entry->gr_passwd);
  mem = caml_copy_string_array_r(ctx, (const char**)entry->gr_mem);
  res = caml_alloc_small_r(ctx, 4, 0);
    Field(res,0) = name;
    Field(res,1) = pass;
    Field(res,2) = Val_int(entry->gr_gid);
    Field(res,3) = mem;
  End_roots();
  return res;
}

CAMLprim value unix_getgrnam_r(CAML_R, value name)
{
  struct group * entry;
  entry = getgrnam(String_val(name));
  if (entry == NULL) caml_raise_not_found_r(ctx);
  return alloc_group_entry_r(ctx, entry);
}

CAMLprim value unix_getgrgid_r(CAML_R, value gid)
{
  struct group * entry;
  entry = getgrgid(Int_val(gid));
  if (entry == NULL) caml_raise_not_found_r(ctx);
  return alloc_group_entry_r(ctx, entry);
}
