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

#include <stdio.h> // FIXME: remove after debugging
#include <mlvalues.h>
#include <alloc.h>
#include <fail.h>
#include <memory.h>
#include <signals.h>
#include "unixsupport.h"

#ifdef HAS_SELECT

#include <sys/types.h>
#include <sys/time.h>
#ifdef HAS_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int fdlist_to_fdset(value fdlist, fd_set *fdset, int *maxfd)
{
  value l;
  FD_ZERO(fdset);
  for (l = fdlist; l != Val_int(0); l = Field(l, 1)) {
    long fd = Long_val(Field(l, 0));
    /* PR#5563: harden against bad fds */
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    FD_SET((int) fd, fdset);
    if (fd > *maxfd) *maxfd = fd;
  }
  return 0;
}

static value fdset_to_fdlist_r(CAML_R, value fdlist, fd_set *fdset)
{
  value l;
  value res = Val_int(0);

  Begin_roots2(l, res);
    for (l = fdlist; l != Val_int(0); l = Field(l, 1)) {
      int fd = Int_val(Field(l, 0));
      if (FD_ISSET(fd, fdset)) {
        value newres = caml_alloc_small_r(ctx,2, 0);
        Field(newres, 0) = Val_int(fd);
        Field(newres, 1) = res;
        res = newres;
      }
    }
  End_roots();
  return res;
}

CAMLprim value unix_select_r(CAML_R, value readfds, value writefds, value exceptfds,
                             value timeout)
{
DUMP("");
  CAMLparam4(readfds, writefds, exceptfds, timeout);
  CAMLlocal1(res);
  fd_set read, write, except;
  int maxfd;
  double tm;
  struct timeval tv;
  struct timeval * tvp;
  int retcode;

  //Begin_roots3 (readfds, writefds, exceptfds);
    maxfd = -1;
    retcode  = fdlist_to_fdset(readfds, &read, &maxfd);
    retcode += fdlist_to_fdset(writefds, &write, &maxfd);
    retcode += fdlist_to_fdset(exceptfds, &except, &maxfd);
//fprintf(stderr, "unix_select_r: context %p, thread %p OK-200\n", ctx, (void*)pthread_self()); fflush(stderr);
    /* PR#5563: if a bad fd was encountered, report EINVAL error */
    if (retcode != 0) unix_error_r(ctx, EINVAL, "select", Nothing);
    tm = Double_val(timeout);
    if (tm < 0.0)
      tvp = (struct timeval *) NULL;
    else {
      tv.tv_sec = (int) tm;
      tv.tv_usec = (int) (1e6 * (tm - tv.tv_sec));
      tvp = &tv;
    }
DUMP("");
    caml_enter_blocking_section_r(ctx);
    retcode = select(maxfd + 1, &read, &write, &except, tvp);
    caml_leave_blocking_section_r(ctx);
DUMP("");
    if (retcode == -1) uerror_r(ctx,"select", Nothing);
    readfds = fdset_to_fdlist_r(ctx, readfds, &read);
    writefds = fdset_to_fdlist_r(ctx, writefds, &write);
    exceptfds = fdset_to_fdlist_r(ctx, exceptfds, &except);
DUMP("before caml_alloc");
//fprintf(stderr, "unix_select_r: context %p, thread %p OK-400: the allocation pointer is %p (limit %p)\n", ctx, (void*)pthread_self(), ctx->caml_young_ptr, ctx->caml_young_limit); fflush(stderr);
    res = caml_alloc_small_r(ctx, 3, 0);
DUMP("after caml_alloc");
//fprintf(stderr, "unix_select_r: context %p, thread %p OK-500: the allocation pointer is %p (limit %p)\n", ctx, (void*)pthread_self(), ctx->caml_young_ptr, ctx->caml_young_limit); fflush(stderr);
    Field(res, 0) = readfds;
    Field(res, 1) = writefds;
    Field(res, 2) = exceptfds;
    //End_roots();
DUMP("end");
  CAMLreturn(res);
}

/* CAMLprim value unix_select_r(CAML_R, value readfds, value writefds, value exceptfds, */
/*                              value timeout) */
/* { */
/*   fd_set read, write, except; */
/*   int maxfd; */
/*   double tm; */
/*   struct timeval tv; */
/*   struct timeval * tvp; */
/*   int retcode; */
/*   value res; */

/*   Begin_roots3 (readfds, writefds, exceptfds); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-100\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     maxfd = -1; */
/*     retcode  = fdlist_to_fdset(readfds, &read, &maxfd); */
/*     retcode += fdlist_to_fdset(writefds, &write, &maxfd); */
/*     retcode += fdlist_to_fdset(exceptfds, &except, &maxfd); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-200\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     /\* PR#5563: if a bad fd was encountered, report EINVAL error *\/ */
/*     if (retcode != 0) unix_error_r(ctx, EINVAL, "select", Nothing); */
/*     tm = Double_val(timeout); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-300\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     if (tm < 0.0) */
/*       tvp = (struct timeval *) NULL; */
/*     else { */
/*       tv.tv_sec = (int) tm; */
/*       tv.tv_usec = (int) (1e6 * (tm - tv.tv_sec)); */
/*       tvp = &tv; */
/*     } */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-400\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     caml_enter_blocking_section_r(ctx); */
/*     retcode = select(maxfd + 1, &read, &write, &except, tvp); */
/*     caml_leave_blocking_section_r(ctx); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-500\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     if (retcode == -1) uerror_r(ctx,"select", Nothing); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-510\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     readfds = fdset_to_fdlist_r(ctx, readfds, &read); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-520\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     writefds = fdset_to_fdlist_r(ctx, writefds, &write); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-530\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     exceptfds = fdset_to_fdlist_r(ctx, exceptfds, &except); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-600\n", ctx, (void*)pthread_self()); fflush(stderr); res = NULL; */
/*     res = caml_alloc_small_r(ctx, 3, 0); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-610\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     Field(res, 0) = readfds; */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-620\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     Field(res, 1) = writefds; */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-630\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*     Field(res, 2) = exceptfds; */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-700\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*   End_roots(); */
/* fprintf(stderr, "unix_select_r: context %p, thread %p OK-800\n", ctx, (void*)pthread_self()); fflush(stderr); */
/*   return res; */
/* } */

#else

CAMLprim value unix_select_r(CAML_R, value readfds, value writefds, value exceptfds,
                           value timeout)
{ caml_invalid_argument_r(ctx,"select not implemented"); }

#endif
