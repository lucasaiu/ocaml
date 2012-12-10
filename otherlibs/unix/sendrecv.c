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

#include <string.h>
#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>
#include <memory.h>
#include <signals.h>
#include "unixsupport.h"

#ifdef HAS_SOCKETS
#include "socketaddr.h"

static int msg_flag_table[] = {
  MSG_OOB, MSG_DONTROUTE, MSG_PEEK
};

CAMLprim value unix_recv_r(CAML_R, value sock, value buff, value ofs, value len,
                         value flags)
{
  int ret, cv_flags;
  long numbytes;
  char iobuf[UNIX_BUFFER_SIZE];

  cv_flags = convert_flag_list(flags, msg_flag_table);
  Begin_root (buff);
    numbytes = Long_val(len);
    if (numbytes > UNIX_BUFFER_SIZE) numbytes = UNIX_BUFFER_SIZE;
    caml_enter_blocking_section_r(ctx);
    ret = recv(Int_val(sock), iobuf, (int) numbytes, cv_flags);
    caml_leave_blocking_section_r(ctx);
    if (ret == -1) uerror_r(ctx,"recv", Nothing);
    memmove (&Byte(buff, Long_val(ofs)), iobuf, ret);
  End_roots();
  return Val_int(ret);
}

CAMLprim value unix_recvfrom_r(CAML_R, value sock, value buff, value ofs, value len,
                             value flags)
{
  int ret, cv_flags;
  long numbytes;
  char iobuf[UNIX_BUFFER_SIZE];
  value res;
  value adr = Val_unit;
  union sock_addr_union addr;
  socklen_param_type addr_len;

  cv_flags = convert_flag_list(flags, msg_flag_table);
  Begin_roots2 (buff, adr);
    numbytes = Long_val(len);
    if (numbytes > UNIX_BUFFER_SIZE) numbytes = UNIX_BUFFER_SIZE;
    addr_len = sizeof(addr);
    caml_enter_blocking_section_r(ctx);
    ret = recvfrom(Int_val(sock), iobuf, (int) numbytes, cv_flags,
                   &addr.s_gen, &addr_len);
    caml_leave_blocking_section_r(ctx);
    if (ret == -1) uerror_r(ctx,"recvfrom", Nothing);
    memmove (&Byte(buff, Long_val(ofs)), iobuf, ret);
    adr = alloc_sockaddr_r(ctx, &addr, addr_len, -1);
    res = caml_alloc_small_r(ctx,2, 0);
    Field(res, 0) = Val_int(ret);
    Field(res, 1) = adr;
  End_roots();
  return res;
}

CAMLprim value unix_send_r(CAML_R, value sock, value buff, value ofs, value len,
                         value flags)
{
  int ret, cv_flags;
  long numbytes;
  char iobuf[UNIX_BUFFER_SIZE];

  cv_flags = convert_flag_list(flags, msg_flag_table);
  numbytes = Long_val(len);
  if (numbytes > UNIX_BUFFER_SIZE) numbytes = UNIX_BUFFER_SIZE;
  memmove (iobuf, &Byte(buff, Long_val(ofs)), numbytes);
  caml_enter_blocking_section_r(ctx);
  ret = send(Int_val(sock), iobuf, (int) numbytes, cv_flags);
  caml_leave_blocking_section_r(ctx);
  if (ret == -1) uerror_r(ctx,"send", Nothing);
  return Val_int(ret);
}

CAMLprim value unix_sendto_native_r(CAML_R, value sock, value buff, value ofs, value len,
                                  value flags, value dest)
{
  int ret, cv_flags;
  long numbytes;
  char iobuf[UNIX_BUFFER_SIZE];
  union sock_addr_union addr;
  socklen_param_type addr_len;

  cv_flags = convert_flag_list(flags, msg_flag_table);
  get_sockaddr_r(ctx, dest, &addr, &addr_len);
  numbytes = Long_val(len);
  if (numbytes > UNIX_BUFFER_SIZE) numbytes = UNIX_BUFFER_SIZE;
  memmove (iobuf, &Byte(buff, Long_val(ofs)), numbytes);
  caml_enter_blocking_section_r(ctx);
  ret = sendto(Int_val(sock), iobuf, (int) numbytes, cv_flags,
               &addr.s_gen, addr_len);
  caml_leave_blocking_section_r(ctx);
  if (ret == -1) uerror_r(ctx,"sendto", Nothing);
  return Val_int(ret);
}

CAMLprim value unix_sendto_r(CAML_R, value *argv, int argc)
{
  return unix_sendto_native_r
    (ctx, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
}

#else

CAMLprim value unix_recv_r(CAML_R, value sock, value buff, value ofs, value len,
                         value flags)
{ caml_invalid_argument_r(ctx,"recv not implemented"); }

CAMLprim value unix_recvfrom_r(CAML_R, value sock, value buff, value ofs, value len,
                             value flags)
{ caml_invalid_argument_r(ctx,"recvfrom not implemented"); }

CAMLprim value unix_send_r(CAML_R, value sock, value buff, value ofs, value len,
                         value flags)
{ caml_invalid_argument_r(ctx,"send not implemented"); }

CAMLprim value unix_sendto_native_r(CAML_R, value sock, value buff, value ofs, value len,
                                  value flags, value dest)
{ caml_invalid_argument_r(ctx,"sendto not implemented"); }

CAMLprim value unix_sendto_r(CAML_R, value *argv, int argc)
{ caml_invalid_argument_r(ctx,"sendto not implemented"); }

#endif
