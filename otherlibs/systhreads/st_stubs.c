//static int already_fully_initialized = 0; // FIXME: remove this horrible kludge

/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1995 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */
#include <assert.h> // FIXME: remove after debugging

#define CAML_CONTEXT_ROOTS /* GC-protection macros */
#define CAML_CONTEXT_FAIL
#define CAML_CONTEXT_BACKTRACE
#define CAML_CONTEXT_SIGNALS
#define CAML_CONTEXT_STACKS
#define CAML_CONTEXT_STARTUP
#define CAML_ST_POSIX_CODE
#include "mlvalues.h" // FIXME: remove after debugging unless needed
#include "context.h" // FIXME: remove after debugging unless needed
#include "context_split.h" // FIXME: remove after debugging unless needed
#include "memory.h" // FIXME: remove after debugging unless needed

#include "alloc.h"
#include "backtrace.h"
#include "callback.h"
#include "custom.h"
#include "fail.h"
#include "io.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "printexc.h"
#include "roots.h"
#include "signals.h"
#ifdef NATIVE_CODE
#include "stack.h"
#else
#include "stacks.h"
#endif
#include "sys.h"
#include "threads.h"

/* Initial size of bytecode stack when a thread is created (4 Ko) */
#define Thread_stack_size (Stack_size / 4)

/* Max computation time before rescheduling, in milliseconds */
#define Thread_timeout 50

/* OS-specific code */
#ifdef _WIN32
#include "st_win32.h"
#else
#include "st_posix.h"
#endif

/* The ML value describing a thread (heap-allocated) */

struct caml_thread_descr {
  value ident;                  /* Unique integer ID */
  value start_closure;          /* The closure to start this thread */
  value terminated;             /* Triggered event for thread termination */
};

#define Ident(v) (((struct caml_thread_descr *)(v))->ident)
#define Start_closure(v) (((struct caml_thread_descr *)(v))->start_closure)
#define Terminated(v) (((struct caml_thread_descr *)(v))->terminated)

/* The infos on threads (allocated via malloc()) */

struct caml_thread_struct {
  value descr;                  /* The heap-allocated descriptor (root) */
  struct caml_thread_struct * next;  /* Double linking of running threads */
  struct caml_thread_struct * prev;
#ifdef NATIVE_CODE
  char * top_of_stack;          /* Top of stack for this thread (approx.) */
  char * bottom_of_stack;       /* Saved value of caml_bottom_of_stack */
  uintnat last_retaddr;         /* Saved value of caml_last_return_address */
  value * gc_regs;              /* Saved value of caml_gc_regs */
  char * exception_pointer;     /* Saved value of caml_exception_pointer */
  struct caml__roots_block * local_roots; /* Saved value of local_roots */
  struct longjmp_buffer * exit_buf; /* For thread exit */
#else
  value * stack_low;            /* The execution stack for this thread */
  value * stack_high;
  value * stack_threshold;
  value * sp;                   /* Saved value of extern_sp for this thread */
  value * trapsp;               /* Saved value of trapsp for this thread */
  struct caml__roots_block * local_roots; /* Saved value of local_roots */
  struct longjmp_buffer * external_raise; /* Saved external_raise */
#endif
  int backtrace_pos;            /* Saved backtrace_pos */
  code_t * backtrace_buffer;    /* Saved backtrace_buffer */
  value backtrace_last_exn;     /* Saved backtrace_last_exn (root) */
  CAML_R;                       /* the context to which this thread belongs */
  //void* posix_thread; // FIXME: for debugging.  REMOVE
  //int id; // FIXME: for debugging only
};

/* The key used for storing the thread descriptor in the specific data
   of the corresponding system thread. */
static st_tlskey thread_descriptor_key;

/* The key used for unlocking I/O channels on exceptions */
static st_tlskey last_channel_locked_key;

/* Identifier for next thread creation; protected by the global lock */
static intnat thread_next_ident = 0;

/* Forward declarations */
static value caml_threadstatus_new_r (CAML_R);
static void caml_threadstatus_terminate_r (CAML_R, value wrapper);
static st_retcode caml_threadstatus_wait_r (CAML_R, value wrapper);

/* Imports from the native-code runtime system */
#ifdef NATIVE_CODE
// FIXME: remove these: they are now in the context
//extern struct longjmp_buffer caml_termination_jmpbuf;
//extern void (*caml_termination_hook)(void);
#endif

/* Hook for scanning the stacks of the other threads */

static void (*prev_scan_roots_hook) (scanning_action);

static void caml_thread_scan_roots(scanning_action action)
{
  QB();
  INIT_CAML_R;
  caml_thread_t th;

  th = curr_thread;
  do {
    (*action)(ctx, th->descr, &th->descr);
    (*action)(ctx, th->backtrace_last_exn, &th->backtrace_last_exn);
    /* Don't rescan the stack of the current thread, it was done already */
    if (th != curr_thread) {
#ifdef NATIVE_CODE
      if (th->bottom_of_stack != NULL)
        caml_do_local_roots_r(ctx, action, th->bottom_of_stack, th->last_retaddr,
                              th->gc_regs, th->local_roots);
#else
      caml_do_local_roots_r(ctx, action, th->sp, th->stack_high, th->local_roots);
#endif
    }
    th = th->next;
  } while (th != curr_thread);
  /* Hook */
  if (prev_scan_roots_hook != NULL) (*prev_scan_roots_hook)(action);
  QR();
}

/* Hooks for enter_blocking_section and leave_blocking_section */

static void caml_thread_enter_blocking_section_hook_default(void)
{
  QB();
  INIT_CAML_R;
  /* Save the stack-related global variables in the thread descriptor
     of the current thread */
#ifdef NATIVE_CODE
  curr_thread->bottom_of_stack = caml_bottom_of_stack;
  curr_thread->last_retaddr = caml_last_return_address;

  //fprintf(stderr, "caml_thread_enter_blocking_section_hook_default: ctx %p, thread %p: curr_thread->gc_regs, about to be overwritten, was %p\n", ctx, (void*)pthread_self(), curr_thread->gc_regs); fflush(stderr);
  //fprintf(stderr, "caml_thread_enter_blocking_section_hook_default: ctx %p, thread %p: caml_gc_regs is %p\n", ctx, (void*)pthread_self(), caml_gc_regs); fflush(stderr);

  curr_thread->gc_regs = caml_gc_regs;
  curr_thread->exception_pointer = caml_exception_pointer;
  curr_thread->local_roots = caml_local_roots;
#else
  curr_thread->stack_low = caml_stack_low;
  curr_thread->stack_high = caml_stack_high;
  curr_thread->stack_threshold = caml_stack_threshold;
  curr_thread->sp = caml_extern_sp;
  curr_thread->trapsp = caml_trapsp;
  curr_thread->local_roots = caml_local_roots;
  curr_thread->external_raise = caml_external_raise;
#endif
  curr_thread->backtrace_pos = caml_backtrace_pos;
  curr_thread->backtrace_buffer = caml_backtrace_buffer;
  curr_thread->backtrace_last_exn = caml_backtrace_last_exn;
  /* Tell other threads that the runtime is free */
  st_masterlock_release(&caml_master_lock);
  QR();
}

static void caml_thread_leave_blocking_section_hook_default(void)
{
  QB();
  INIT_CAML_R;
  /* Wait until the runtime is free */
  st_masterlock_acquire(&caml_master_lock);
  /* Update curr_thread to point to the thread descriptor corresponding
     to the thread currently executing */
  curr_thread = st_tls_get(thread_descriptor_key);
  /* Restore the stack-related global variables */
#ifdef NATIVE_CODE
  caml_bottom_of_stack= curr_thread->bottom_of_stack;
  caml_last_return_address = curr_thread->last_retaddr;

  //fprintf(stderr, "caml_thread_leave_blocking_section_hook_default: ctx %p, thread %p: caml_gc_regs, about to be overwritten, was %p\n", ctx, (void*)pthread_self(), caml_gc_regs); fflush(stderr);
  //fprintf(stderr, "caml_thread_leave_blocking_section_hook_default: ctx %p, thread %p: curr_thread->gc_regs is %p\n", ctx, (void*)pthread_self(), curr_thread->gc_regs); fflush(stderr);

  caml_gc_regs = curr_thread->gc_regs;
  caml_exception_pointer = curr_thread->exception_pointer;
  caml_local_roots = curr_thread->local_roots;
#else
  caml_stack_low = curr_thread->stack_low;
  caml_stack_high = curr_thread->stack_high;
  caml_stack_threshold = curr_thread->stack_threshold;
  caml_extern_sp = curr_thread->sp;
  caml_trapsp = curr_thread->trapsp;
  caml_local_roots = curr_thread->local_roots;
  caml_external_raise = curr_thread->external_raise;
#endif
  caml_backtrace_pos = curr_thread->backtrace_pos;
  caml_backtrace_buffer = curr_thread->backtrace_buffer;
  caml_backtrace_last_exn = curr_thread->backtrace_last_exn;
  QR();
}

static int caml_thread_try_leave_blocking_section(void)
{
  QBR();
  /* Disable immediate processing of signals (PR#3659).
     try_leave_blocking_section always fails, forcing the signal to be
     recorded and processed at the next leave_blocking_section or
     polling. */
  return 0;
}

/* Hooks for I/O locking */

static void caml_io_mutex_free(struct channel *chan)
{
  QB();
  st_mutex mutex = chan->mutex;
  if (mutex != NULL) st_mutex_destroy(mutex);
  QR();
}

static void caml_io_mutex_lock(struct channel *chan)
{
  QB();
  INIT_CAML_R;
  st_mutex mutex = chan->mutex;

  if (mutex == NULL) {
    st_mutex_create(&mutex);
    chan->mutex = mutex;
  }
  /* PR#4351: first try to acquire mutex without releasing the master lock */
  if (st_mutex_trylock(mutex) == PREVIOUSLY_UNLOCKED) {
    st_tls_set(last_channel_locked_key, (void *) chan);
    QR();
    return;
  }
  /* If unsuccessful, block on mutex */
  caml_enter_blocking_section_r(ctx);
  st_mutex_lock(mutex);
  /* Problem: if a signal occurs at this point,
     and the signal handler raises an exception, we will not
     unlock the mutex.  The alternative (doing the setspecific
     before locking the mutex is also incorrect, since we could
     then unlock a mutex that is unlocked or locked by someone else. */
  st_tls_set(last_channel_locked_key, (void *) chan);
  caml_leave_blocking_section_r(ctx);
  QR();
}

static void caml_io_mutex_unlock(struct channel *chan)
{
  QB();
  st_mutex_unlock(chan->mutex);
  st_tls_set(last_channel_locked_key, NULL);
  QR();
}

static void caml_io_mutex_unlock_exn(void)
{
  QB();
  struct channel * chan = st_tls_get(last_channel_locked_key);
  if (chan != NULL) caml_io_mutex_unlock(chan);
  QR();
}

/* Hook for estimating stack usage */

static uintnat (*prev_stack_usage_hook)(void);

static uintnat caml_thread_stack_usage(void)
{
  QB();
  uintnat sz;
  caml_thread_t th;
  INIT_CAML_R;

  /* Don't add stack for current thread, this is done elsewhere */
  for (sz = 0, th = curr_thread->next;
       th != curr_thread;
       th = th->next) {
#ifdef NATIVE_CODE
    sz += (value *) th->top_of_stack - (value *) th->bottom_of_stack;
#else
    sz += th->stack_high - th->sp;
#endif
  }
  if (prev_stack_usage_hook != NULL)
    sz += prev_stack_usage_hook();
  QR();
  return sz;
}

/* Create and setup a new thread info block.
   This block has no associated thread descriptor and
   is not inserted in the list of threads. */

static caml_thread_t caml_thread_new_info(void)
{
  QB();
  caml_thread_t th;

  th = (caml_thread_t) malloc(sizeof(struct caml_thread_struct));
  if (th == NULL) return NULL;
  //memset(th, 0, sizeof(struct caml_thread_struct)); // This was for debugging only --Luca Saiu REENTRANTRUNTIME
  memset(th, 0xaa, sizeof(struct caml_thread_struct)); // This was for debugging only --Luca Saiu REENTRANTRUNTIME
/*   //memset(th, 42, sizeof(struct caml_thread_struct)); // This was for debugging only --Luca Saiu REENTRANTRUNTIME */
/*   memset(th, -1, sizeof(struct caml_thread_struct)); // This was for debugging only --Luca Saiu REENTRANTRUNTIME */
/*   {int i; */
/*     char *th_as_char_star = (char*)th; */
/*     for(i = 0; i < sizeof(struct caml_thread_struct); i ++) */
/*       if((i % 8) == 0){ */
/*         int j; */
/*         for(j = 0; j < 8; j ++) */
/*           th_as_char_star[i + j] = 0xff; */
/*         th_as_char_star[i + 0] = 0xaa; */
/*         th_as_char_star[i + 1] = i/4; */
/*         th_as_char_star[i + 6] = i/4; */
/*         th_as_char_star[i + 7] = 0xee; */
/*         // !!!!!!!!!!!!! */
/*       } */
/* #ifdef NATIVE_CODE */
/*     //th->gc_regs = (void*)(long)0xffffffffffffffff; */
/*     //th->gc_regs = (void*)(long)0xaaaaaaaaaaaaaaaa; */
/*     //th->gc_regs = 0; */
/*     th->last_retaddr =      (long)0xaaaaaaaaaaaaaaaa; */
/*     th->gc_regs =           (void*)(long)0xbbbbbbbbbbbbbbbb;//(void*)(long)0xaabbccddffffffff; */
/*     th->exception_pointer = (void*)(long)0xcccccccccccccccc; */
/* #endif // #ifdef NATIVE_CODE */
/*     //memset(th, 254, sizeof(struct caml_thread_struct)); // This was for debugging only --Luca Saiu REENTRANTRUNTIME */
/*     for(i = 0; i < sizeof(struct caml_thread_struct); i ++) */
/*       if((i % 8) == 0){ */
/*         long *p = (long*)(th_as_char_star + i); */
/*         fprintf(stderr, "%4i. %4i. Q %20li 0x%-20lx\n", i, i/8, *p, *p); fflush(stderr); */
/*       } */
/*   } */

  th->descr = Val_unit;         /* filled later */
#ifdef NATIVE_CODE
  th->bottom_of_stack = NULL;
  th->top_of_stack = NULL;
  th->last_retaddr = 1;
  th->exception_pointer = NULL;
  th->local_roots = NULL;
  th->exit_buf = NULL;
#else
  /* Allocate the stacks */
  th->stack_low = (value *) stat_alloc(Thread_stack_size);
  th->stack_high = th->stack_low + Thread_stack_size / sizeof(value);
  th->stack_threshold = th->stack_low + Stack_threshold / sizeof(value);
  th->sp = th->stack_high;
  th->trapsp = th->stack_high;
  th->local_roots = NULL;
  th->external_raise = NULL;
#endif
  th->backtrace_pos = 0;
  th->backtrace_buffer = NULL;
  th->backtrace_last_exn = Val_unit;
  th->ctx = (void*)(long)0xdead; /* an intentionally invalid value, to aid debugging */
  //th->posix_thread = (pthread_t)(void*)0xbadbadbad; /* an intentionally invalid value, to aid debugging */
  //th->id = (int)-2;
  QR();
  return th;
}

/* Allocate a thread descriptor block. */

static value caml_thread_new_descriptor_r(CAML_R, value clos)
{
  QB();
  value mu = Val_unit;
  value descr;
  Begin_roots2 (clos, mu)
    /* Create and initialize the termination semaphore */
    mu = caml_threadstatus_new_r(ctx);
    /* Create a descriptor for the new thread */
    descr = caml_alloc_small_r(ctx, 3, 0);
    caml_acquire_global_lock();
    Ident(descr) = Val_long(thread_next_ident);
    thread_next_ident++;
    caml_release_global_lock();
    Start_closure(descr) = clos;
    Terminated(descr) = mu;
  End_roots();
  QR();
  return descr;
}

/* Remove a thread info block from the list of threads.
   Free it and its stack resources. */

static void caml_thread_remove_info(caml_thread_t th)
{
  //QBR("############################################################# \"virtually\" removing %p from the thread list", th); return; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  QR();
  CAML_R = th->ctx;

  /* If we're destroying the current thread, invalidate ctx->curr_thread: */
  if(curr_thread == th) curr_thread = NULL; // Luca Saiu REENTRANTRUNTIME (after a similar change by Fabrice)

  if (th->next == th)
    all_threads = NULL; /* last OCaml thread exiting */
  else if (all_threads == th)
    all_threads = th->next;     /* PR#5295 */
  th->next->prev = th->prev;
  th->prev->next = th->next;
#ifndef NATIVE_CODE
  stat_free(th->stack_low);
#endif
  if (th->backtrace_buffer != NULL) free(th->backtrace_buffer);
  memset(th, 0xdd, sizeof(struct caml_thread_struct)); // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! To ease debugging !!!!!!!!!!!!!!!
  stat_free(th);
  QR();
}

/* Reinitialize the thread machinery after a fork() (PR#4577) */

static void caml_thread_reinitialize(void)
{
  QB();
  caml_thread_t thr, next;
  struct channel * chan;
  INIT_CAML_R;
  DUMP("re-initializing the thread machinery after a fork");
  DUMP("FIXME: I suppose something horrible will happen soon. --Luca Saiu REENTRANTRUNTIME");

  /* Remove all other threads (now nonexistent)
     from the doubly-linked list of threads */
  thr = curr_thread->next;
  while (thr != curr_thread) {
    next = thr->next;
    stat_free(thr);
    thr = next;
  }
  curr_thread->next = curr_thread;
  curr_thread->prev = curr_thread;
  all_threads = curr_thread;
  /* Reinitialize the master lock machinery,
     just in case the fork happened while other threads were doing
     leave_blocking_section */
  st_masterlock_init(&caml_master_lock);
  /* Tick thread is not currently running in child process, will be
     re-created at next Thread.create */
  caml_tick_thread_running = 0;
  /* Destroy all IO mutexes; will be reinitialized on demand */
  caml_acquire_global_lock();
  for (chan = caml_all_opened_channels;
       chan != NULL;
       chan = chan->next) {
    if (chan->mutex != NULL) {
      st_mutex_destroy(chan->mutex);
      chan->mutex = NULL;
    }
  }
  caml_release_global_lock();
  QR();
}

static void caml_thread_initialize_for_current_context_r(CAML_R){
  QB();
  /* The thing is already initialized if ctx->curr_thread is NULL: */
  if(curr_thread != NULL){
    QR();
    return;
  }

  /* Set up the hooks */
  caml_enter_blocking_section_hook = caml_thread_enter_blocking_section_hook_default;
  caml_leave_blocking_section_hook = caml_thread_leave_blocking_section_hook_default;
  caml_try_leave_blocking_section_hook = caml_thread_try_leave_blocking_section;

  /* Set up a thread info block for the current thread */
  curr_thread =
    (caml_thread_t) stat_alloc(sizeof(struct caml_thread_struct));
  memset(curr_thread, 0xaa, sizeof(struct caml_thread_struct)); // !!!!!!!!! FIXME: remove.  This is for debugging only
  //memset(curr_thread, 0x00, sizeof(struct caml_thread_struct)); // !!!!!!!!! FIXME: remove.  This is for debugging only
  curr_thread->descr = caml_thread_new_descriptor_r(ctx, Val_unit);
  curr_thread->next = curr_thread;
  curr_thread->prev = curr_thread;
  all_threads = curr_thread;
  curr_thread->backtrace_last_exn = Val_unit;
#ifdef NATIVE_CODE
  curr_thread->exit_buf = &caml_termination_jmpbuf;
#endif
  curr_thread->ctx = ctx;

  DUMP("*********************************************** caml_thread_initialize_for_current_context_r");
  /* If this is not the main context, then we have to copy its signal
     handlers (a Caml array, which can be cloned via a blob): */
  if(ctx->descriptor->kind != caml_global_context_main){
    DUMP("ctx->caml_signal_handlers is %p", (void*)ctx->caml_signal_handlers);
    assert(ctx->caml_signal_handlers == 0);
    char *blob = caml_serialize_into_blob_r(the_main_context, the_main_context->caml_signal_handlers);
    ctx->caml_signal_handlers = caml_deserialize_blob_r(ctx, blob);
    free(blob);
    //caml_register_global_root_r(ctx, &ctx->caml_signal_handlers); // moved to context.h, at context creation time
  }
  /* The stack-related fields will be filled in at the next
     enter_blocking_section */
  /* Associate the thread descriptor with the thread */
  st_tls_set(thread_descriptor_key, (void *) curr_thread);
  QR();
}

/* static int caml_systhreads_get_thread_no_r(CAML_R); */

/* Initialize the global thread machinery */

CAMLprim value caml_thread_initialize_r(CAML_R, value unit)   /* ML */
{
  QB();
  /* Protect against repeated initialization (PR#1325) */
  static int already_initialized = 0;
  if(already_initialized) {QR(); return Val_unit;} else already_initialized = 1;

  /* caml_set_caml_get_thread_no_r(ctx, caml_systhreads_get_thread_no_r); */
  caml_set_caml_initialize_context_thread_support(ctx, caml_thread_initialize_for_current_context_r);

  /* OS-specific initialization */
  st_initialize();
  /* Initialize and acquire the master lock */
  st_masterlock_init(&caml_master_lock);
  /* Initialize the keys */
  st_tls_newkey(&thread_descriptor_key);
  st_tls_newkey(&last_channel_locked_key);

  /* Set up the hooks */
  prev_scan_roots_hook = caml_scan_roots_hook;
  caml_scan_roots_hook = caml_thread_scan_roots;
  // ???? MOVED, TENTATIVELY
  //caml_enter_blocking_section_hook = caml_thread_enter_blocking_section_hook_default;
  //caml_leave_blocking_section_hook = caml_thread_leave_blocking_section_hook_default;
  //caml_try_leave_blocking_section_hook = caml_thread_try_leave_blocking_section;
#ifdef NATIVE_CODE
  caml_termination_hook = st_thread_exit;
#endif
  caml_channel_mutex_free = caml_io_mutex_free;
  caml_channel_mutex_lock = caml_io_mutex_lock;
  caml_channel_mutex_unlock = caml_io_mutex_unlock;
  caml_channel_mutex_unlock_exn = caml_io_mutex_unlock_exn;
  prev_stack_usage_hook = caml_stack_usage_hook;
  caml_stack_usage_hook = caml_thread_stack_usage;
  /* Set up fork() to reinitialize the thread machinery in the child
     (PR#4577) */
  st_atfork(caml_thread_reinitialize);

  /* We've set up the whole machinery which will be used from now on
     at context split; good, but we also have to initialize the
     *current* context: */
  caml_initialize_context_thread_support(ctx);

  QR();
  return Val_unit;
}

/* Cleanup the thread machinery on program exit or DLL unload. */

CAMLprim value caml_thread_cleanup_r(CAML_R, value unit)   /* ML */
{
  QB();
  if (caml_tick_thread_running) st_thread_kill(caml_tick_thread_id);
  QR();
  return Val_unit;
}

/* Thread cleanup at termination */

static void caml_thread_stop_r(CAML_R)
{
  QB();
#ifndef NATIVE_CODE
  /* PR#5188: update curr_thread->stack_low because the stack may have
     been reallocated since the last time we entered a blocking section */
  curr_thread->stack_low = caml_stack_low;
#endif
  /* Signal that the thread has terminated */
  caml_threadstatus_terminate_r(ctx, Terminated(curr_thread->descr));
  /* Remove th from the doubly-linked list of threads and free its info block */
  caml_thread_remove_info(curr_thread);
  curr_thread = NULL; // Fabrice Le Fessant REENTRANTRUNTIME
  /* OS-specific cleanups */
  st_thread_cleanup();
  /* Release the runtime system */
  st_masterlock_release(&caml_master_lock);
  QR();
}

/* /\* Return the number of threads associated to the given context: *\/ */
/* static int caml_systhreads_get_thread_no_r(CAML_R){ */
/*   QB(); */
/*   int result = 0; */
/*   caml_thread_t t = all_threads; */
/*   caml_thread_t first_thread = t; */
/*   if(t == NULL) */
/*     result = 0;//return 0; */
/*   else do{ */
/*       result ++; */
/*       t = t->next; */
/*       if(result > 1000) */
/*         QDUMP("EEEEEEEEEEEEEEEEEEEEEE probably looping (1) looking for %p", first_thread); */
/*     } while(t != first_thread); */

/*   QR(); */
/*   return result; */
/* } */

/* Create a thread */

static ST_THREAD_FUNCTION caml_thread_start(void * arg)
{
  QB();
  caml_thread_t th = (caml_thread_t) arg;
  CAML_R = th->ctx;
  //CAMLparam();
  //CAMLlocal1(clos);
  value clos;
#ifdef NATIVE_CODE
  struct longjmp_buffer termination_buf;
  char tos;
#endif
  /* associate the context to this thread */
  caml_set_thread_local_context(ctx);

  /* Associate the thread descriptor with the thread */
  st_tls_set(thread_descriptor_key, (void *) th);
  /* Acquire the global mutex */
  caml_leave_blocking_section_r(ctx);
#ifdef NATIVE_CODE
  /* Record top of stack (approximative) */
  th->top_of_stack = &tos;
  /* Setup termination handler (for caml_thread_exit) */
  if (sigsetjmp(termination_buf.buf, 0) == 0) {
    th->exit_buf = &termination_buf;
#endif
    /* Callback the closure */
    clos = Start_closure(th->descr);
  Begin_root (clos);
    caml_modify_r(ctx, &(Start_closure(th->descr)), Val_unit);
    DUMP("calling the caml code");
    //caml_callback_exn_r(ctx, clos, Val_unit); // !!!!!!!!!!!!!!!!!!!!!!!!!!!! This is the right version
    caml_callback_r(ctx, clos, Val_unit); // Just for testing: I want to see the exception !!!!!!!!!!!!!!!!!!!!!!!!
    QR("exiting the native-code thread");
  End_roots();
    caml_thread_stop_r(ctx);
#ifdef NATIVE_CODE
  }
#endif
  /* The thread now stops running */
  QR();
  return 0;
  //CAMLreturnT(void*, 0);
}

/* static ST_THREAD_FUNCTION caml_thread_start(void * arg) */
/* { */
/*   QB(); */
/*   caml_thread_t th = (caml_thread_t) arg; */
/*   CAML_R = th->ctx; */
/*   value clos; */
/* #ifdef NATIVE_CODE */
/*   struct longjmp_buffer termination_buf; */
/*   char tos; */
/* #endif */
/*   /\* associate the context to this thread *\/ */
/*   caml_set_thread_local_context(ctx); */

/*   /\* Associate the thread descriptor with the thread *\/ */
/*   st_tls_set(thread_descriptor_key, (void *) th); */
/*   /\* Acquire the global mutex *\/ */
/*   caml_leave_blocking_section_r(ctx); */
/* #ifdef NATIVE_CODE */
/*   /\* Record top of stack (approximative) *\/ */
/*   th->top_of_stack = &tos; */
/*   /\* Setup termination handler (for caml_thread_exit) *\/ */
/*   if (sigsetjmp(termination_buf.buf, 0) == 0) { */
/*     th->exit_buf = &termination_buf; */
/* #endif */
/*     /\* Callback the closure *\/ */
/*     clos = Start_closure(th->descr); */
/*     caml_modify_r(ctx, &(Start_closure(th->descr)), Val_unit); */
/*     //caml_callback_exn_r(ctx, clos, Val_unit); // !!!!!!!!!!!!!!!!!!!!!!!!!!!! This is the right version */
/*     caml_callback_r(ctx, clos, Val_unit); // Just for testing: I want to see the exception !!!!!!!!!!!!!!!!!!!!!!!! */
/*     QR("exiting the native-code thread"); */
/*     caml_thread_stop_r(ctx); */
/* #ifdef NATIVE_CODE */
/*   } */
/* #endif */
/*   /\* The thread now stops running *\/ */
/*   QR(); */
/*   return 0; */
/*   //CAMLreturnT(void*, 0); */
/* } */

CAMLprim value caml_thread_new_r(CAML_R, value clos)          /* ML */
{
  QB();
  CAMLparam1(clos);
  caml_thread_t th;
  st_retcode err;

  ctx->can_split = 0;

  /* Create a thread info block */
  th = caml_thread_new_info();
  if (th == NULL) caml_raise_out_of_memory_r(ctx);
  /* Equip it with a thread descriptor */
  th->descr = caml_thread_new_descriptor_r(ctx, clos);
  /* Add thread info block to the list of threads */
  th->next = curr_thread->next;
  th->prev = curr_thread;
  th->ctx = ctx;
  curr_thread->next->prev = th;
  curr_thread->next = th;
  /* Create the new thread */
  err = st_thread_create_r(ctx, NULL, caml_thread_start, (void *) th, "ordinary");
  if (err != 0) {
    /* Creation failed, remove thread info block from list of threads */
    caml_thread_remove_info(th);
    st_check_error_r(ctx, err, "Thread.create");
  }
  /* Create the tick thread if not already done.
     Because of PR#4666, we start the tick thread late, only when we create
     the first additional thread in the current process*/
  if (! caml_tick_thread_running) {
    err = st_thread_create_r(ctx, &caml_tick_thread_id, caml_thread_tick, ctx, "tick");
    st_check_error_r(ctx, err, "Thread.create");
    caml_tick_thread_running = 1;
  }
  QR();
  CAMLreturn(th->descr);
}


/* Register a thread already created from C */

CAMLexport int caml_c_thread_register_r(CAML_R)
{
  QB();
  caml_thread_t th;
  st_retcode err;

  /* Already registered? */
  if (st_tls_get(thread_descriptor_key) != NULL) {QR(); return 0;}

  /* Create a thread info block */
  th = caml_thread_new_info();
  if (th == NULL) {QR(); return 0;}
#ifdef NATIVE_CODE
  th->top_of_stack = (char *) &err;
#endif
  assert(th->ctx == (void*)0xdead);
  th->ctx = ctx;
  /* Take master lock to protect access to the chaining of threads */
  st_masterlock_acquire(&caml_master_lock);
  /* Add thread info block to the list of threads */
  if (all_threads == NULL) {
    th->next = th;
    th->prev = th;
    all_threads = th;
  } else {
    th->next = all_threads->next;
    th->prev = all_threads;
    all_threads->next->prev = th;
    all_threads->next = th;
  }
  /* Associate the thread descriptor with the thread */
  st_tls_set(thread_descriptor_key, (void *) th);
  /* Release the master lock */
  st_masterlock_release(&caml_master_lock);
  /* Now we can re-enter the run-time system and heap-allocate the descriptor */
  caml_leave_blocking_section_r(ctx);
  th->descr = caml_thread_new_descriptor_r(ctx, Val_unit);  /* no closure */
  /* Create the tick thread if not already done.  */
  if (! caml_tick_thread_running) {
    err = st_thread_create_r(ctx, &caml_tick_thread_id, caml_thread_tick, ctx, "tick");
    if (err == 0) caml_tick_thread_running = 1;
  }
  /* Exit the run-time system */
  caml_enter_blocking_section_r(ctx);
  QR();
  return 1;
}

/* Unregister a thread that was created from C and registered with
   the function above */

CAMLexport int caml_c_thread_unregister_r(CAML_R)
{
  //QBR("############################################################# \"virtually\" calling caml_c_thread_unregister_r"); return; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  caml_thread_t th = st_tls_get(thread_descriptor_key);
  assert(ctx == th->ctx);// FIXME: remove

  /* Not registered? */
  if (th == NULL) {QR(); return 0;}
  /* Wait until the runtime is available */
  st_masterlock_acquire(&caml_master_lock);
  /* Forget the thread descriptor */
  st_tls_set(thread_descriptor_key, NULL);
  /* Remove thread info block from list of threads, and free it */
  caml_thread_remove_info(th);
  /* Release the runtime */
  st_masterlock_release(&caml_master_lock);
  QR();
  return 1;
}

/* Return the current thread */

CAMLprim value caml_thread_self_r(CAML_R, value unit)         /* ML */
{
  QB();
  if (curr_thread == NULL) {QR(); caml_invalid_argument_r(ctx, "Thread.self: not initialized");};
  QR();
  return curr_thread->descr;
}

/* Return the identifier of a thread */

CAMLprim value caml_thread_id(value th)          /* ML */
{
  QBR();
  return Ident(th);
}

/* Print uncaught exception and backtrace */

CAMLprim value caml_thread_uncaught_exception_r(CAML_R, value exn)  /* ML */
{
  QB();
  char * msg = caml_format_exception_r(ctx, exn);
DUMP("Thread %d killed on uncaught exception %s", Int_val(Ident(curr_thread->descr)), msg); // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  free(msg);
  if (caml_backtrace_active) caml_print_exception_backtrace_r(ctx);
  fflush(stderr);
  QR();
  return Val_unit;
}

/* Terminate current thread */

CAMLprim value caml_thread_exit_r(CAML_R, value unit)   /* ML */
{
  QB();
  struct longjmp_buffer * exit_buf = NULL;

  if (curr_thread == NULL) {QR(); caml_invalid_argument_r(ctx, "Thread.exit: not initialized");}

  /* In native code, we cannot call pthread_exit here because on some
     systems this raises a C++ exception, and ocamlopt-generated stack
     frames cannot be unwound.  Instead, we longjmp to the thread
     creation point (in caml_thread_start) or to the point in
     caml_main where caml_termination_hook will be called.
     Note that threads created in C then registered do not have
     a creation point (exit_buf == NULL).
 */
#ifdef NATIVE_CODE
  exit_buf = curr_thread->exit_buf;
#endif
  caml_thread_stop_r(ctx);
  if (exit_buf != NULL) {
    /* Native-code and (main thread or thread created by OCaml) */
    siglongjmp(exit_buf->buf, 1);
  } else {
    /* Bytecode, or thread created from C */
    st_thread_exit();
  }
  QR();
  return Val_unit;  /* not reached */
}

/* Allow re-scheduling */

CAMLprim value caml_thread_yield_r(CAML_R, value unit)        /* ML */
{
  QB();
  int st_masterlock_waiters_result = st_masterlock_waiters(&caml_master_lock);
  if (st_masterlock_waiters_result == 0){
    QR();
    return Val_unit;
  }
  caml_enter_blocking_section_r(ctx);
  st_thread_yield();
  caml_leave_blocking_section_r(ctx);
  QR();
  return Val_unit;
}

/* Suspend the current thread until another thread terminates */

CAMLprim value caml_thread_join_r(CAML_R, value th)          /* ML */
{
  QB();
  st_retcode rc = caml_threadstatus_wait_r(ctx, Terminated(th));
  st_check_error_r(ctx, rc, "Thread.join");
  QR();
  return Val_unit;
}

/* Mutex operations */

#define Mutex_val(v) (* ((st_mutex *) Data_custom_val(v)))
#define Max_mutex_number 5000

static void caml_mutex_finalize(value wrapper)
{
  QBR();
  st_mutex_destroy(Mutex_val(wrapper));
}

static int caml_mutex_compare(value wrapper1, value wrapper2)
{
  QB();
  st_mutex mut1 = Mutex_val(wrapper1);
  st_mutex mut2 = Mutex_val(wrapper2);
  QR();
  return mut1 == mut2 ? 0 : mut1 < mut2 ? -1 : 1;
}

static intnat caml_mutex_hash(value wrapper)
{
  QBR();
  return (intnat) (Mutex_val(wrapper));
}

static struct custom_operations caml_mutex_ops = {
  "_mutex",
  caml_mutex_finalize,
  caml_mutex_compare,
  caml_mutex_hash,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default, // FIXME: this was not present before.  Is there a reason? --Luca Saiu REENTRANTRUNTIME
  custom_serialize_default,
  custom_deserialize_default
};

CAMLprim value caml_mutex_new_r(CAML_R, value unit)        /* ML */
{
  QB();
  st_mutex mut = NULL;          /* suppress warning */
  value wrapper;
  st_check_error_r(ctx, st_mutex_create(&mut), "Mutex.create");
  wrapper = alloc_custom(&caml_mutex_ops, sizeof(st_mutex *),
                         1, Max_mutex_number);
  Mutex_val(wrapper) = mut;
  QR();
  return wrapper;
}

CAMLprim value caml_mutex_lock_r(CAML_R, value wrapper)     /* ML */
{
  QB();
  st_mutex mut = Mutex_val(wrapper);
  st_retcode retcode;

  /* PR#4351: first try to acquire mutex without releasing the master lock */
  if (st_mutex_trylock(mut) == PREVIOUSLY_UNLOCKED) {QR(); return Val_unit;}
  /* If unsuccessful, block on mutex */
  Begin_root(wrapper)           /* prevent the deallocation of mutex */
    caml_enter_blocking_section_r(ctx);
    retcode = st_mutex_lock(mut);
    caml_leave_blocking_section_r(ctx);
  End_roots();
  st_check_error_r(ctx, retcode, "Mutex.lock");
  QR();
  return Val_unit;
}

CAMLprim value caml_mutex_unlock_r(CAML_R, value wrapper)           /* ML */
{
  QB();
  st_mutex mut = Mutex_val(wrapper);
  st_retcode retcode;
  /* PR#4351: no need to release and reacquire master lock */
  retcode = st_mutex_unlock(mut);
  st_check_error_r(ctx, retcode, "Mutex.unlock");
  QR();
  return Val_unit;
}

CAMLprim value caml_mutex_try_lock_r(CAML_R, value wrapper)           /* ML */
{
  QB();
  st_mutex mut = Mutex_val(wrapper);
  st_retcode retcode;
  retcode = st_mutex_trylock(mut);
  if (retcode == ALREADY_LOCKED) {QR(); return Val_false;}
  st_check_error_r(ctx, retcode, "Mutex.try_lock");
  QR();
  return Val_true;
}

/* Conditions operations */

#define Condition_val(v) (* (st_condvar *) Data_custom_val(v))
#define Max_condition_number 5000

static void caml_condition_finalize(value wrapper)
{
  QB();
  st_condvar_destroy(Condition_val(wrapper));
  QR();
}

static int caml_condition_compare(value wrapper1, value wrapper2)
{
  QB();
  st_condvar cond1 = Condition_val(wrapper1);
  st_condvar cond2 = Condition_val(wrapper2);
  QR();
  return cond1 == cond2 ? 0 : cond1 < cond2 ? -1 : 1;
}

static intnat caml_condition_hash(value wrapper)
{
  QBR();
  return (intnat) (Condition_val(wrapper));
}

static struct custom_operations caml_condition_ops = {
  "_condition",
  caml_condition_finalize,
  caml_condition_compare,
  caml_condition_hash,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default,
  custom_serialize_default,
  custom_deserialize_default
};

CAMLprim value caml_condition_new_r(CAML_R, value unit)        /* ML */
{
  QB();
  st_condvar cond = NULL;       /* suppress warning */
  value wrapper;
  st_check_error_r(ctx, st_condvar_create(&cond), "Condition.create");
  wrapper = alloc_custom(&caml_condition_ops, sizeof(st_condvar *),
                         1, Max_condition_number);
  Condition_val(wrapper) = cond;
  QR();
  return wrapper;
}

CAMLprim value caml_condition_wait_r(CAML_R, value wcond, value wmut)           /* ML */
{
  QB();
  st_condvar cond = Condition_val(wcond);
  st_mutex mut = Mutex_val(wmut);
  st_retcode retcode;

  Begin_roots2(wcond, wmut)     /* prevent deallocation of cond and mutex */
    caml_enter_blocking_section_r(ctx);
    retcode = st_condvar_wait(cond, mut);
    caml_leave_blocking_section_r(ctx);
  End_roots();
  st_check_error_r(ctx, retcode, "Condition.wait");
  QR();
  return Val_unit;
}

CAMLprim value caml_condition_signal_r(CAML_R, value wrapper)           /* ML */
{
  QBR();
  st_check_error_r(ctx, st_condvar_signal(Condition_val(wrapper)),
                 "Condition.signal");
  return Val_unit;
}

CAMLprim value caml_condition_broadcast_r(CAML_R, value wrapper)           /* ML */
{
  QB();
  st_check_error_r(ctx, st_condvar_broadcast(Condition_val(wrapper)),
                 "Condition.signal");
  QR();
  return Val_unit;
}

/* Thread status blocks */

#define Threadstatus_val(v) (* ((st_event *) Data_custom_val(v)))
#define Max_threadstatus_number 500

static void caml_threadstatus_finalize(value wrapper)
{
  QB();
  st_event_destroy(Threadstatus_val(wrapper));
  QR();
}

static int caml_threadstatus_compare(value wrapper1, value wrapper2)
{
  QB();
  st_event ts1 = Threadstatus_val(wrapper1);
  st_event ts2 = Threadstatus_val(wrapper2);
  QR();
  return ts1 == ts2 ? 0 : ts1 < ts2 ? -1 : 1;
}

static struct custom_operations caml_threadstatus_ops = {
  "_threadstatus",
  caml_threadstatus_finalize,
  caml_threadstatus_compare,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default,
  custom_serialize_default,
  custom_deserialize_default
};

static value caml_threadstatus_new_r (CAML_R)
{
  QB();
  st_event ts = NULL;           /* suppress warning */
  value wrapper;
  st_check_error_r(ctx, st_event_create(&ts), "Thread.create");
  wrapper = alloc_custom(&caml_threadstatus_ops, sizeof(st_event *),
                         1, Max_threadstatus_number);
  Threadstatus_val(wrapper) = ts;
  QR();
  return wrapper;
}

static void caml_threadstatus_terminate_r (CAML_R, value wrapper)
{
  QB();
  st_event_trigger(Threadstatus_val(wrapper));
  QR();
}

static st_retcode caml_threadstatus_wait_r (CAML_R, value wrapper)
{
  QB();
  st_event ts = Threadstatus_val(wrapper);
  st_retcode retcode;

  Begin_roots1(wrapper)         /* prevent deallocation of ts */
    caml_enter_blocking_section_r(ctx);
    retcode = st_event_wait(ts);
    caml_leave_blocking_section_r(ctx);
  End_roots();
  QR();
  return retcode;
}
