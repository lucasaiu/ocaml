/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Signal handling, code common to the bytecode and native systems */

#include <stdio.h>

#define CAML_CONTEXT_MINOR_GC
#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_SIGNALS_BYT
#define CAML_CONTEXT_SIGNALS

#include <signal.h>
#include "alloc.h"
#include "callback.h"
#include "config.h"
#include "fail.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "roots.h"
#include "signals.h"
#include "signals_machdep.h"
#include "sys.h"

/* Execute all pending signals */

void caml_process_pending_signals_r(CAML_R)
{
  int i;

  if (caml_signals_are_pending) {
    caml_signals_are_pending = 0;
    for (i = 0; i < NSIG; i++) {
      if (caml_pending_signals[i]) {
        caml_pending_signals[i] = 0;
        caml_execute_signal_r(ctx, i, 0);
      }
    }
  }
}

/* Record the delivery of a signal, and arrange for it to be processed
   as soon as possible:
   - in bytecode: via caml_something_to_do, processed in caml_process_event
   - in native-code: by playing with the allocation limit, processed
       in caml_garbage_collection
*/

void caml_record_signal_r(CAML_R, int signal_number)
{
  caml_pending_signals[signal_number] = 1;
  caml_signals_are_pending = 1;
#ifndef NATIVE_CODE
  caml_something_to_do = 1;
#else
  caml_young_limit = caml_young_end;
#endif
}

/* Management of blocking sections. */

void caml_enter_blocking_section_default(void)
{
  INIT_CAML_R;
  /* fprintf(stderr, "caml_enter_blocking_section_default %x\n", ctx); */
  Assert (caml_async_signal_mode == 0);
  caml_async_signal_mode = 1;
}

void caml_leave_blocking_section_default(void)
{
  INIT_CAML_R;
  /*  fprintf(stderr, "caml_leave_blocking_section_default %x\n", ctx); */
  Assert (caml_async_signal_mode == 1);
  caml_async_signal_mode = 0;
}

int caml_try_leave_blocking_section_default(void)
{
  intnat res;
  INIT_CAML_R;
  /* fprintf(stderr, "caml_try_leave_blocking_section_default\n"); */
  Read_and_clear(res, caml_async_signal_mode);
  return res;
}

CAMLexport void caml_enter_blocking_section_r(CAML_R)
{
  /*  fprintf(stderr, "caml_enter_blocking_section_r %lx...\n", ctx);
  fprintf(stderr, "enter_blocking_section_hook = %lx (%lx)\n",
	  & caml_enter_blocking_section_hook,
	  caml_enter_blocking_section_hook);
  fprintf(stderr, "leave_blocking_section_hook = %lx (%lx)\n",
	  & caml_leave_blocking_section_hook,
	  caml_leave_blocking_section_hook);
  fprintf(stderr, "caml_enter_blocking_section_default = %lx\n",
	  caml_enter_blocking_section_default);
  fprintf(stderr, "caml_leave_blocking_section_default = %lx\n",
	  caml_leave_blocking_section_default);
  */

  while (1){
    /* Process all pending signals now */
    /*    fprintf(stderr, "caml_process_pending_signals_r...\n"); */
    caml_process_pending_signals_r(ctx);
    /*    fprintf(stderr, "caml_enter_blocking_section_hook...\n"); */
    Assert ( caml_enter_blocking_section_hook != caml_leave_blocking_section_hook );
    caml_enter_blocking_section_hook ();
    /* Check again for pending signals.
       If none, done; otherwise, try again */
    /*    fprintf(stderr, "!caml_signals_are_pending ? %d...\n", caml_signals_are_pending); */
    if (! caml_signals_are_pending) break;
    /* fprintf(stderr, "caml_leave_blocking_section_hook...\n"); */
    caml_leave_blocking_section_hook ();
  }
  /* fprintf(stderr, "caml_enter_blocking_section_r DONE\n"); */

}

CAMLexport void caml_leave_blocking_section_r(CAML_R)
{
  caml_leave_blocking_section_hook ();
  caml_process_pending_signals_r(ctx);
}

/* Execute a signal handler immediately */

void caml_execute_signal_r(CAML_R, int signal_number, int in_signal_handler)
{
  //DUMP("signal_number %i (converted into %i), in_signal_handler=%i", signal_number, (int)caml_rev_convert_signal_number(signal_number), in_signal_handler);
  value res;
#ifdef POSIX_SIGNALS
  sigset_t sigs;
  /* Block the signal before executing the handler, and record in sigs
     the original signal mask */
  sigemptyset(&sigs);
  sigaddset(&sigs, signal_number);
  sigprocmask(SIG_BLOCK, &sigs, &sigs);
#endif
  //DUMP("right before calling caml_callback_exn_r; caml_signal_handlers is %p", (void*)caml_signal_handlers);
  res = caml_callback_exn_r(ctx,
           Field(caml_signal_handlers, signal_number),
           Val_int(caml_rev_convert_signal_number(signal_number)));
  //DUMP("back from caml_callback_exn_r; caml_signal_handlers is %p", caml_signal_handlers);
#ifdef POSIX_SIGNALS
  if (! in_signal_handler) {
    /* Restore the original signal mask */
    sigprocmask(SIG_SETMASK, &sigs, NULL);
  } else if (Is_exception_result(res)) {
    /* Restore the original signal mask and unblock the signal itself */
    sigdelset(&sigs, signal_number);
    sigprocmask(SIG_SETMASK, &sigs, NULL);
  }
#endif
  if (Is_exception_result(res)) caml_raise_r(ctx, Extract_exception(res));
}

/* Arrange for a garbage collection to be performed as soon as possible */

void caml_urge_major_slice_r (CAML_R)
{
  caml_force_major_slice = 1;
#ifndef NATIVE_CODE
  caml_something_to_do = 1;
#else
  caml_young_limit = caml_young_end;
  /* This is only moderately effective on ports that cache [caml_young_limit]
     in a register, since [caml_modify] is called directly, not through
     [caml_c_call], so it may take a while before the register is reloaded
     from [caml_young_limit]. */
#endif
}

/* OS-independent numbering of signals */

#ifndef SIGABRT
#define SIGABRT -1
#endif
#ifndef SIGALRM
#define SIGALRM -1
#endif
#ifndef SIGFPE
#define SIGFPE -1
#endif
#ifndef SIGHUP
#define SIGHUP -1
#endif
#ifndef SIGILL
#define SIGILL -1
#endif
#ifndef SIGINT
#define SIGINT -1
#endif
#ifndef SIGKILL
#define SIGKILL -1
#endif
#ifndef SIGPIPE
#define SIGPIPE -1
#endif
#ifndef SIGQUIT
#define SIGQUIT -1
#endif
#ifndef SIGSEGV
#define SIGSEGV -1
#endif
#ifndef SIGTERM
#define SIGTERM -1
#endif
#ifndef SIGUSR1
#define SIGUSR1 -1
#endif
#ifndef SIGUSR2
#define SIGUSR2 -1
#endif
#ifndef SIGCHLD
#define SIGCHLD -1
#endif
#ifndef SIGCONT
#define SIGCONT -1
#endif
#ifndef SIGSTOP
#define SIGSTOP -1
#endif
#ifndef SIGTSTP
#define SIGTSTP -1
#endif
#ifndef SIGTTIN
#define SIGTTIN -1
#endif
#ifndef SIGTTOU
#define SIGTTOU -1
#endif
#ifndef SIGVTALRM
#define SIGVTALRM -1
#endif
#ifndef SIGPROF
#define SIGPROF -1
#endif

static int posix_signals[] = {
  SIGABRT, SIGALRM, SIGFPE, SIGHUP, SIGILL, SIGINT, SIGKILL, SIGPIPE,
  SIGQUIT, SIGSEGV, SIGTERM, SIGUSR1, SIGUSR2, SIGCHLD, SIGCONT,
  SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGVTALRM, SIGPROF
};

CAMLexport int caml_convert_signal_number(int signo)
{
  if (signo < 0 && signo >= -(sizeof(posix_signals) / sizeof(int)))
    return posix_signals[-signo-1];
  else
    return signo;
}

CAMLexport int caml_rev_convert_signal_number(int signo)
{
  int i;
  for (i = 0; i < sizeof(posix_signals) / sizeof(int); i++)
    if (signo == posix_signals[i]) return -i - 1;
  return signo;
}

/* Installation of a signal handler (as per [Sys.signal]) */

CAMLprim value caml_install_signal_handler_r(CAML_R, value signal_number, value action)
{
  CAMLparam2 (signal_number, action);
  CAMLlocal1 (res);
  int sig, act, oldact;

  sig = caml_convert_signal_number(Int_val(signal_number));
  if (sig < 0 || sig >= NSIG)
    caml_invalid_argument_r(ctx, "Sys.signal: unavailable signal");
  switch(action) {
  case Val_int(0):              /* Signal_default */
    act = 0;
    break;
  case Val_int(1):              /* Signal_ignore */
    act = 1;
    break;
  default:                      /* Signal_handle */
    act = 2;
    break;
  }
  oldact = caml_set_signal_action(sig, act);
  switch (oldact) {
  case 0:                       /* was Signal_default */
    res = Val_int(0);
    break;
  case 1:                       /* was Signal_ignore */
    res = Val_int(1);
    break;
  case 2:                       /* was Signal_handle */
    res = caml_alloc_small_r (ctx, 1, 0);
    Field(res, 0) = Field(caml_signal_handlers, sig);
    break;
  default:                      /* error in caml_set_signal_action */
    caml_sys_error_r(ctx, NO_ARG);
  }
  if (Is_block(action)) {
    if (caml_signal_handlers == 0) {
      caml_signal_handlers = caml_alloc_r(ctx, NSIG, 0);
      //// !!!!!!!!!!!!!!!!!!!!!!!!!!! This is probably ok.  Just remove my comment markers. !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      int i;
      for(i = 0; i < NSIG; i ++)
        caml_initialize_r(ctx, &Field(caml_signal_handlers, i), Val_int(0));
      ////
      //caml_register_global_root_r(ctx, &caml_signal_handlers); // Moved to context.c, at context creation time
    }
    caml_modify_r(ctx, &Field(caml_signal_handlers, sig), Field(action, 0));
    //DUMP("Registering the handler %p (%p) for the signal %i", (void*)action, (void*)Field(action, 0), sig);
  }
  caml_process_pending_signals_r(ctx);
  CAMLreturn (res);
}
