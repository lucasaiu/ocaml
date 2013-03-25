//#warning Do "git diff 7d4891a0395abdac8e60f3cc788b71908c73a88d" on this file, and read it top-to-bottom
/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2009 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */
#include <assert.h> // FIXME: remove after debugging

/* POSIX thread implementation of the "st" interface */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#ifdef __sun
#define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>
#include <sys/time.h>
#ifdef __linux__
#include <unistd.h>
#endif

#ifdef __GNUC__
#define INLINE inline
#else
#define INLINE
#endif

typedef int st_retcode;

#define SIGPREEMPTION SIGVTALRM

/* OS-specific initialization */

static int st_initialize(void)
{
  return 0;
}

/* Thread creation.  Created in detached mode if [res] is NULL. */

//typedef pthread_t st_thread_id;

static int st_thread_create_r(CAML_R, st_thread_id * res,
                              void * (*fn)(void *), void * arg)
{
  QB();
DUMP("about to create a new thread");
  pthread_t thr;
  pthread_attr_t attr;
  int rc;

  pthread_attr_init(&attr);
  if (res == NULL) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  rc = pthread_create(&thr, &attr, fn, arg);
  if (res != NULL) *res = thr;
  QR();
  return rc;
}

#define ST_THREAD_FUNCTION void *

/* Cleanup at thread exit */

static INLINE void st_thread_cleanup(void)
{
  QBR("this does nothing");
  return;
}

/* Thread termination */

static void st_thread_exit(void)
{
  QBR();
  pthread_exit(NULL);
}

static void st_thread_kill(st_thread_id thr)
{
  QBR();
  pthread_cancel(thr);
}

/* Scheduling hints */

static void INLINE st_thread_yield(void)
{
  QB();
#ifndef __linux__
  /* sched_yield() doesn't do what we want in Linux 2.6 and up (PR#2663) */
  sched_yield();
#endif
  QR();
}

/* Thread-specific state */

typedef pthread_key_t st_tlskey;

static int st_tls_newkey(st_tlskey * res)
{
  QBR();
  return pthread_key_create(res, NULL);
}

static INLINE void * st_tls_get(st_tlskey k)
{
  QBR();
  return pthread_getspecific(k);
}

static INLINE void st_tls_set(st_tlskey k, void * v)
{
  QB();
  pthread_setspecific(k, v);
  QR();
}

static void st_masterlock_init(st_masterlock * m)
{
  QB();
  INIT_CAML_R;
  assert(m == &caml_master_lock);
  DUMP("st_masterlock_init: initialized the masterlock at %p", m);
  assert(m == &caml_master_lock);
  pthread_mutex_init(&m->lock, NULL);
  pthread_cond_init(&m->is_free, NULL);
  m->busy = 1;
  m->waiters = 0;
  QR();
}

static void st_masterlock_acquire(st_masterlock * m)
{
  QB();
  INIT_CAML_R; //fprintf(stderr, "Context %p: st_masterlock_acquire: thread %p\n", ctx, (void*)pthread_self()); fflush(stderr);
  assert(m == &caml_master_lock);
//fprintf(stderr, "Context %p: st_masterlock_acquire: thread %p: beginning\n", ctx, (void*)pthread_self()); fflush(stderr);
  pthread_mutex_lock(&m->lock);
  while (m->busy) {
//fprintf(stderr, "Context %p: st_masterlock_acquire: thread %p is waiting...\n", ctx, (void*)pthread_self()); fflush(stderr);
    m->waiters ++;
    pthread_cond_wait(&m->is_free, &m->lock);
    m->waiters --;
//fprintf(stderr, "Context %p: st_masterlock_acquire: thread %p has waited...\n", ctx, (void*)pthread_self()); fflush(stderr);
  }
  m->busy = 1;
  pthread_mutex_unlock(&m->lock);
//fprintf(stderr, "Context %p: st_masterlock_acquire: thread %p: end\n", ctx, (void*)pthread_self()); fflush(stderr);
  QR();
}

static void st_masterlock_release(st_masterlock * m)
{
  QB();
  INIT_CAML_R; //fprintf(stderr, "Context %p: st_masterlock_release: thread %p\n", ctx, (void*)pthread_self()); fflush(stderr);
  assert(m == &caml_master_lock);
  pthread_mutex_lock(&m->lock);
  m->busy = 0;
  pthread_mutex_unlock(&m->lock);
  pthread_cond_signal(&m->is_free);
  QR();
}

static INLINE int st_masterlock_waiters(st_masterlock * m)
{
  QBR();
  return m->waiters;
}

/* Mutexes */

typedef pthread_mutex_t * st_mutex;

static int st_mutex_create(st_mutex * res)
{
  QB();
  int rc;
  st_mutex m = malloc(sizeof(pthread_mutex_t));
  if (m == NULL){ QR(); return ENOMEM;}
  rc = pthread_mutex_init(m, NULL);
  if (rc != 0) { free(m); QR(); return rc; }
  *res = m;
  QR();
  return 0;
}

static int st_mutex_destroy(st_mutex m)
{
  QB();
   int rc;
  rc = pthread_mutex_destroy(m);
  free(m);
  QR();
  return rc;
}

static INLINE int st_mutex_lock(st_mutex m)
{
  QBR();
  return pthread_mutex_lock(m);
}

#define PREVIOUSLY_UNLOCKED 0
#define ALREADY_LOCKED EBUSY

static INLINE int st_mutex_trylock(st_mutex m)
{
  QBR();
  return pthread_mutex_trylock(m);
}

static INLINE int st_mutex_unlock(st_mutex m)
{
  QBR();
  return pthread_mutex_unlock(m);
}

/* Condition variables */

typedef pthread_cond_t * st_condvar;

static int st_condvar_create(st_condvar * res)
{
  QB();
  int rc;
  st_condvar c = malloc(sizeof(pthread_cond_t));
  if (c == NULL) {QR(); return ENOMEM;}
  rc = pthread_cond_init(c, NULL);
  if (rc != 0) { free(c); QR(); return rc; }
  *res = c;
  QR();
  return 0;
}

static int st_condvar_destroy(st_condvar c)
{
  QB();
  int rc;
  rc = pthread_cond_destroy(c);
  free(c);
  QR();
  return rc;
}

static INLINE int st_condvar_signal(st_condvar c)
{
  QBR();
  return pthread_cond_signal(c);
}

static INLINE int st_condvar_broadcast(st_condvar c)
{
  QBR();
  return pthread_cond_broadcast(c);
}

static INLINE int st_condvar_wait(st_condvar c, st_mutex m)
{
  QBR();
  return pthread_cond_wait(c, m);
}

/* Triggered events */

typedef struct st_event_struct {
  pthread_mutex_t lock;         /* to protect contents */
  int status;                   /* 0 = not triggered, 1 = triggered */
  pthread_cond_t triggered;     /* signaled when triggered */
} * st_event;

static int st_event_create(st_event * res)
{
  QB();
  int rc;
  st_event e = malloc(sizeof(struct st_event_struct));
  if (e == NULL) {QR();return ENOMEM;}
  rc = pthread_mutex_init(&e->lock, NULL);
  if (rc != 0) { free(e); QR();return rc; }
  rc = pthread_cond_init(&e->triggered, NULL);
  if (rc != 0) { pthread_mutex_destroy(&e->lock); free(e); QR();return rc; }
  e->status = 0;
  *res = e;
  QR();
  return 0;
}

static int st_event_destroy(st_event e)
{
  QB();
  int rc1, rc2;
  rc1 = pthread_mutex_destroy(&e->lock);
  rc2 = pthread_cond_destroy(&e->triggered);
  free(e);
  QR();
  return rc1 != 0 ? rc1 : rc2;
}

static int st_event_trigger(st_event e)
{
  QB();
  int rc;
  rc = pthread_mutex_lock(&e->lock);
  if (rc != 0) {QR();return rc;}
  e->status = 1;
  rc = pthread_mutex_unlock(&e->lock);
  if (rc != 0) {QR();return rc;}
  rc = pthread_cond_broadcast(&e->triggered);
  QR();
  return rc;
}

static int st_event_wait(st_event e)
{
  QB();
  int rc;
  rc = pthread_mutex_lock(&e->lock);
  if (rc != 0) {QR();return rc;}
  while(e->status == 0) {
    rc = pthread_cond_wait(&e->triggered, &e->lock);
    if (rc != 0) {QR();return rc;}
  }
  rc = pthread_mutex_unlock(&e->lock);
  QR();
  return rc;
}

/* Reporting errors */

static void st_check_error_r(CAML_R, int retcode, char * msg)
{
  QB();
  char * err;
  int errlen, msglen;
  value str;

  if (retcode == 0) {QR();return;}
  if (retcode == ENOMEM) {QR();caml_raise_out_of_memory_r(ctx);}
  err = strerror(retcode);
  msglen = strlen(msg);
  errlen = strlen(err);
  str = caml_alloc_string_r(ctx, msglen + 2 + errlen);
  memmove (&Byte(str, 0), msg, msglen);
  memmove (&Byte(str, msglen), ": ", 2);
  memmove (&Byte(str, msglen + 2), err, errlen);
  QR();
  caml_raise_sys_error_r(ctx, str);
}

/* The tick thread: posts a SIGPREEMPTION signal periodically */

static void * caml_thread_tick(void * context_as_void_star)
{
  QB();
  CAML_R = context_as_void_star;
  struct timeval timeout;
  sigset_t mask;
//DUMP("start");

  /* Block all signals so that we don't try to execute an OCaml signal handler*/
  sigfillset(&mask);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);
  /* Allow async cancellation */
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  while(1) {
    /* select() seems to be the most efficient way to suspend the
       thread for sub-second intervals */
    timeout.tv_sec = 0;//2;//timeout.tv_sec = 0; // FIXME: this of course should be reset to 0 after debugging
    timeout.tv_usec = Thread_timeout * 1000;
//DUMP("calling select");
    select(0, NULL, NULL, NULL, &timeout);
//DUMP("about to allocate something, just to stress the system");
//    caml_alloc_tuple_r(ctx, 16); // FIXME: remove: this is gratuitous, just to stress the system
//DUMP("ticking (SIGPREEMPTION is %i)", (int)SIGPREEMPTION);
  /* The preemption signal should never cause a callback, so don't
     go through caml_handle_signal(), just record signal delivery via
     caml_record_signal(). */
//fprintf(stderr, "Context %p: st_thread_tick: thread %p ticking.\n", ctx, (void*)pthread_self()); fflush(stderr);
    caml_record_signal_r(ctx, SIGPREEMPTION);
//fprintf(stderr, "Context %p: st_thread_tick: thread %p ticked.\n", ctx, (void*)pthread_self()); fflush(stderr);
  }
  QR();
  return NULL;                  /* prevents compiler warning */
}

/* "At fork" processing */

static int st_atfork(void (*fn)(void))
{
  QBR();
  return pthread_atfork(NULL, NULL, fn);
}

/* Signal handling */

static void st_decode_sigset(value vset, sigset_t * set)
{
  QB();
  sigemptyset(set);
  while (vset != Val_int(0)) {
    int sig = caml_convert_signal_number(Int_val(Field(vset, 0)));
    sigaddset(set, sig);
    vset = Field(vset, 1);
  }
  QR();
}

#ifndef NSIG
#define NSIG 64
#endif

static value st_encode_sigset_r(CAML_R, sigset_t * set)
{
  QB();
  value res = Val_int(0);
  //CAMLparam0();
  //CAMLlocal1(res);
  int i;

  Begin_root(res)
    for (i = 1; i < NSIG; i++)
      if (sigismember(set, i) > 0) {
        value newcons = caml_alloc_small_r(ctx, 2, 0);
        Field(newcons, 0) = Val_int(caml_rev_convert_signal_number(i));
        Field(newcons, 1) = res;
        res = newcons;
      }
  End_roots();
  QR();
  return res;
  //CAMLreturn(res);
}

static int sigmask_cmd[3] = { SIG_SETMASK, SIG_BLOCK, SIG_UNBLOCK };

value caml_thread_sigmask_r(CAML_R, value cmd, value sigs) /* ML */
{
  QB();
  int how;
  sigset_t set, oldset;
  int retcode;

  how = sigmask_cmd[Int_val(cmd)];
  st_decode_sigset(sigs, &set);
  caml_enter_blocking_section_r(ctx);
  retcode = pthread_sigmask(how, &set, &oldset);
  caml_leave_blocking_section_r(ctx);
  st_check_error_r(ctx, retcode, "Thread.sigmask");
  QR();
  return st_encode_sigset_r(ctx, &oldset);
}

value caml_wait_signal_r(CAML_R, value sigs) /* ML */
{
QB();
#ifdef HAS_SIGWAIT
  sigset_t set;
  int retcode, signo;

  st_decode_sigset(sigs, &set);
  caml_enter_blocking_section_r(ctx);
  retcode = sigwait(&set, &signo);
  caml_leave_blocking_section_r(ctx);
  st_check_error_r(ctx, retcode, "Thread.wait_signal");
  QR();
  return Val_int(signo);
#else
  QR();
  invalid_argument("Thread.wait_signal not implemented");
  QR();
  return Val_int(0);            /* not reached */
#endif
}
