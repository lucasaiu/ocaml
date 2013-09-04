/***********************************************************************/
/*                                                                     */
/*                           Objective Caml                            */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/***********************************************************************/
/*                                                                     */
/*          Reentrant patch written by Fabrice Le Fessant              */
/*                                                                     */
/*                         OCamlPro SAS (2011)                         */
/*                                                                     */
/***********************************************************************/

#include <stdio.h> /* for printf */
/* #include <stdio.h> */
//#include <stddef.h> /* for offsetof */
#include <unistd.h> // for sleep
#include <assert.h> // FIXME: remove unless unsed in the end
#include <string.h>
#include <sys/sysinfo.h> // for sysconf
#include "mlvalues.h"
#include "gc.h"
#include "startup.h"
#include "config.h"
#include "signals.h"
#include "memory.h"
#include "fail.h"
#include "callback.h" // for caml_callback_r
#include "alloc.h"
#include "intext.h"
#ifdef HAS_PTHREAD
#include <pthread.h>
#endif // #ifdef HAS_PTHREAD
#include <errno.h> // for EBUSY.  FIXME: ensure this is still needed at the end --Luca Saiu REENTRANTRUNTIME

#ifndef HAS_MULTICONTEXT
caml_global_context the_one_and_only_context_struct;
//caml_global_context * const ctx = the_one_and_only_context_struct;
#endif // #ifndef HAS_MULTICONTEXT

static __thread caml_global_context *the_thread_local_caml_context = NULL;

/* The one and only main context: */
caml_global_context *the_main_context = NULL;

#ifdef HAS_MULTICONTEXT
/* In single-context mode, this is a trivial macro instead of a function. */
caml_global_context *caml_get_thread_local_context(void)
{
  return the_thread_local_caml_context;
}
#endif // #ifdef HAS_MULTICONTEXT

void caml_set_thread_local_context(caml_global_context *new_caml_context)
{
  the_thread_local_caml_context = new_caml_context;
}

extern void caml_enter_blocking_section_default(void);
extern void caml_leave_blocking_section_default(void);
extern int caml_try_leave_blocking_section_default(void);


#ifdef NATIVE_CODE
extern char caml_globals_map[];
#endif

/* static */ /* !!!!!!!!!!!!!!! */ int caml_are_mutexes_already_initialized = 0;

/* The global lock: */
static pthread_mutex_t caml_global_mutex;
static pthread_mutex_t caml_channel_mutex /* __attribute__((unused)) */;
//static int caml_are_global_mutexes_initialized = 0; // FIXME: will this be needed in the end?

void caml_initialize_mutex(pthread_mutex_t *mutex){
#ifdef HAS_PTHREAD
  pthread_mutexattr_t attributes;
  pthread_mutexattr_init(&attributes);
  //int result = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE_NP);
  int result = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE_NP);
  if(result){
    fprintf(stderr, "++++++++ [thread %p] pthread_mutexattr_settype failed\n", (void*)(pthread_self())); fflush(stderr);
    exit(EXIT_FAILURE);
  }
  pthread_mutex_init(mutex, &attributes);
  //fprintf(stderr, "= {%u %p | %p}\n", mutex->__data.__count, (void*)(long)mutex->__data.__count, (void*)(pthread_self())); fflush(stderr);
  pthread_mutexattr_destroy(&attributes);
#endif // #ifdef HAS_PTHREAD
}

void caml_finalize_mutex(pthread_mutex_t *mutex){
#ifdef HAS_PTHREAD
  pthread_mutex_destroy(mutex);
#endif // #ifdef HAS_PTHREAD
}

void caml_initialize_semaphore(sem_t *semaphore, int initial_value){
#ifdef HAS_PTHREAD
  int init_result = sem_init(semaphore, /*not process-shared*/0, initial_value);
  if(init_result != 0){
    fprintf(stderr, "++++++++ [thread %p] sem_init failed\n", (void*)(pthread_self())); fflush(stderr);
    exit(EXIT_FAILURE);
  }
#endif // #ifdef HAS_PTHREAD
}
void caml_finalize_semaphore(sem_t *semaphore){
#ifdef HAS_PTHREAD
  sem_destroy(semaphore);
#endif // #ifdef HAS_PTHREAD
}

void caml_p_semaphore(sem_t* semaphore){
#ifdef HAS_PTHREAD
  int sem_wait_result;
  while((sem_wait_result = sem_wait(semaphore)) != 0){
    assert(errno == EINTR);
    INIT_CAML_R; DUMP("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! sem_wait was interrupted by a signal");
    errno = 0;
  }
  assert(sem_wait_result == 0);
#endif // #ifdef HAS_PTHREAD
}
void caml_v_semaphore(sem_t* semaphore){
#ifdef HAS_PTHREAD
  int sem_post_result = sem_post(semaphore);
  assert(sem_post_result == 0);
#endif // #ifdef HAS_PTHREAD
}

#ifdef HAS_PTHREAD
void* caml_destructor_thread_function(void *ctx_as_void_star){
  CAML_R = ctx_as_void_star;
  DUMP("Hello from the destructor thread for context %p (ctx is %p)", ctx_as_void_star, ctx);

  /* Block until notified by a V: */
  DUMP("waiting to be notified before destroying the context");
  caml_p_semaphore(&ctx->destruction_semaphore);

  /* We were notified; run at_exit callbacks and destroy the context: */
  caml_run_at_context_exit_functions_r(ctx);
  DUMP("about to destroy the context");
  caml_destroy_context_r(ctx);
  fprintf(stderr, "Destroyed the context %p: exiting the destructor thread %p as well.\n", ctx, (void*)pthread_self());  fflush(stderr);
  return NULL; // unused
}
#endif // #ifdef HAS_PTHREAD

/* Initialize the given context structure, which has already been allocated elsewhere: */
void caml_initialize_first_global_context(/* CAML_R */caml_global_context *this_ctx)
{
#ifndef HAS_MULTICONTEXT
  /* If we're working with some context different from the one and
     only, we're doing something wrong: */
  assert(this_ctx == &the_one_and_only_context_struct);
#endif // #ifndef HAS_MULTICONTEXT

  /* Maybe we should use partial contexts for specific tasks, that
will probably not be used by all threads.  We should check the size of
each part of the context, to allocate only what is probably required
by all threads, and then allocate other sub-contexts on demand. */

  /*
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT  --Luca Saiu REENTRANTRUNTIME: BEGIN
FIXME: This is a pretty bad symptom.  If I replace the 0 with a 1, the
thing always crashes; but the memset call was just there for
defensiveness, [I suppose, actually, the memset call has been there
since the original version by Fabrice]... There is some struct field
which is never correctly initialized.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT  --Luca Saiu REENTRANTRUNTIME: END
*/
  //memset(this_ctx, 1, sizeof(caml_global_context));
  //memset(this_ctx, -1, sizeof(caml_global_context));
  memset(this_ctx, 0, sizeof(caml_global_context));

#ifdef NATIVE_CODE
  /* TODO : only for first context ! We should implement a way, so
that calling several times caml_main will actually start several
runtimes, with different contexts. A thread would then be able to
schedule several ocaml runtimes ! Protect this with a protected
section.  */
  this_ctx->caml_globals_map = caml_globals_map; // FIXME: this is Fabrice's version; I really have no reason to change it, except to see the effect --Luca Saiu REENTRANTRUNTIME
  //this_ctx->caml_globals_map = NULL; // FIXME: horrible, horrible test.  I'm intentionally breaking Fabrice's code to see what breaks [nothing, apparently].  --Luca Saiu REENTRANTRUNTIME
#endif/* #ifdef NATIVE_CODE */

  /* from stacks.c */
  /*  value caml_global_data; */
  this_ctx->caml_stack_usage_hook = NULL;
  /*  this_ctx->caml_stack_low;
  this_ctx->caml_stack_high;
  this_ctx->caml_stack_threshold;
  this_ctx->caml_extern_sp;
  this_ctx->caml_trapsp;
  this_ctx->caml_trap_barrier;
  this_ctx->caml_max_stack_size;
  */

  /* from major_gc.c */
  /*  this_ctx->caml_percent_free;
  this_ctx->caml_major_heap_increment;
  this_ctx->caml_heap_start;
  this_ctx->caml_gc_sweep_hp;
  this_ctx->caml_gc_phase;
  this_ctx->gray_vals;
  this_ctx->gray_vals_cur;
  this_ctx->gray_vals_end;
  this_ctx->gray_vals_size;
  this_ctx->heap_is_pure;
  this_ctx->caml_allocated_words;
  this_ctx->caml_dependent_size;
  this_ctx->caml_dependent_allocated;
  this_ctx->caml_extra_heap_resources;
*/
  this_ctx->caml_fl_size_at_phase_change = 0;
  /*
  this_ctx->markhp;
  this_ctx->mark_chunk;
  this_ctx->mark_limit;
  this_ctx->caml_gc_subphase;
  this_ctx->weak_prev;
  */
#ifdef DEBUG
  this_ctx->major_gc_counter = 0;
#endif

  /* from freelist.c */
  this_ctx->sentinel.filler1 = 0;
  this_ctx->sentinel.h = Make_header (0, 0, Caml_blue);
  this_ctx->sentinel.first_bp = 0;
  this_ctx->sentinel.filler2 = 0;
#define Fl_head ((char *) (&(this_ctx->sentinel.first_bp)))
  this_ctx->fl_prev = Fl_head;
  this_ctx->fl_last = NULL;
  this_ctx->caml_fl_merge = Fl_head;
  this_ctx->caml_fl_cur_size = 0;
  /*  this_ctx->last_fragment; */
  /*  this_ctx->flp [FLP_MAX]; */
  this_ctx->flp_size = 0;
  this_ctx->beyond = NULL;
  this_ctx->caml_allocation_policy = Policy_next_fit;

  /* from minor_gc.c */
  /*  this_ctx->caml_minor_heap_size; */
  this_ctx->caml_young_base = NULL;
  this_ctx->caml_young_start = NULL;
  this_ctx->caml_young_end = NULL;
  this_ctx->caml_young_ptr = NULL;
  this_ctx->caml_young_limit = NULL;

  this_ctx->caml_ref_table.base = NULL;
  this_ctx->caml_ref_table.end = NULL;
  this_ctx->caml_ref_table.threshold = NULL;
  this_ctx->caml_ref_table.ptr = NULL;
  this_ctx->caml_ref_table.limit = NULL;
  this_ctx->caml_ref_table.size = 0;
  this_ctx->caml_ref_table.reserve = 0;

  this_ctx->caml_weak_ref_table.base = NULL;
  this_ctx->caml_weak_ref_table.end = NULL;
  this_ctx->caml_weak_ref_table.threshold = NULL;
  this_ctx->caml_weak_ref_table.ptr = NULL;
  this_ctx->caml_weak_ref_table.limit = NULL;
  this_ctx->caml_weak_ref_table.size = 0;
  this_ctx->caml_weak_ref_table.reserve = 0;
  this_ctx->caml_in_minor_collection = 0;
  this_ctx->oldify_todo_list = 0;
#ifdef DEBUG
  this_ctx->minor_gc_counter = 0;
#endif

#if 0
  /* from memory.h */
#ifdef ARCH_SIXTYFOUR
  /* this_ctx->caml_page_table */
#else
  /*  this_ctx->caml_page_table[Pagetable1_size]; */
  this_ctx->caml_page_table_empty[0] = 0;
#endif/* #else (#ifdef ARCH_SIXTYFOUR) */
#endif/* #if 0 */

  /* from roots.c */
#ifdef NATIVE_CODE
  this_ctx->caml_local_roots = NULL;
  //this_ctx->caml_scan_roots_hook = NULL;
  /* Fabrice's original version; see my comment in context.h --Luca Saiu REENTRANTRUNTIME */
  /*  this_ctx->caml_top_of_stack; */
  /* this_ctx->caml_bottom_of_stack = NULL; */
  /* this_ctx->caml_last_return_address = 1; */
  /* /\*  this_ctx->caml_gc_regs; */
  /*     this_ctx->caml_globals_inited; *\/ */
  /* this_ctx->caml_globals_scanned = 0; */
  /* this_ctx->caml_dyn_globals = NULL; */
  /* this_ctx->caml_top_of_stack; */
  this_ctx->caml_bottom_of_stack = NULL; /* no stack initially */
  this_ctx->caml_last_return_address = 1; /* not in OCaml code initially */
  /* this_ctx->caml_gc_regs; */
  this_ctx->caml_globals_inited = 0;
  this_ctx->caml_globals_scanned = 0;
  this_ctx->caml_dyn_globals = NULL;
  this_ctx->caml_stack_usage_hook = NULL;
#else
  this_ctx->caml_local_roots = NULL;
  //this_ctx->caml_scan_roots_hook = NULL;
#endif/* #else (#ifdef NATIVE_CODE) */


#ifdef CAML_CONTEXT_STARTUP
  /* from startup.c */
#ifdef NATIVE_CODE
  /* this_ctx->caml_atom_table
     this_ctx->caml_code_area_start
     this_ctx->caml_code_area_end */
  /*  this_ctx->caml_termination_jmpbuf */
  this_ctx->caml_termination_hook = NULL;
#endif/* #ifdef NATIVE_CODE */
#endif/* #ifdef CAML_CONTEXT_STARTUP */

  /* from globroots.c */
  this_ctx->random_seed = 0;

  this_ctx->caml_global_roots.root = NULL;
  this_ctx->caml_global_roots.forward[0] = NULL;
  this_ctx->caml_global_roots.level = 0;
  
  this_ctx->caml_global_roots_young.root = NULL;
  this_ctx->caml_global_roots_young.forward[0] = NULL;
  this_ctx->caml_global_roots_young.level = 0;
  
  this_ctx->caml_global_roots_old.root = NULL;
  this_ctx->caml_global_roots_old.forward[0] = NULL;
  this_ctx->caml_global_roots_old.level = 0;
  
  /* from fail.c */
#ifdef NATIVE_CODE
  this_ctx->caml_exception_pointer= NULL;
  this_ctx->array_bound_error_bucket_inited = 0;
#else
  this_ctx->caml_external_raise = NULL;
  /* this_ctx->caml_exn_bucket */
  this_ctx->out_of_memory_bucket.hdr = 0; this_ctx->out_of_memory_bucket.exn = 0;
#endif /* #else (#ifdef NATIVE_CODE) */

  /* from signals_byt.c */
  this_ctx->caml_something_to_do = 0;
  this_ctx->caml_async_action_hook = NULL;

  /* from signals.c */
  this_ctx->caml_signals_are_pending = 0;
  /* this_ctx->caml_pending_signals */
  this_ctx->caml_async_signal_mode = 0;

  /* this_ctx->caml_enter_blocking_section_hook = &caml_enter_blocking_section_default; */
  /* this_ctx->caml_leave_blocking_section_hook = &caml_leave_blocking_section_default; */
  /* this_ctx->caml_try_leave_blocking_section_hook = &caml_try_leave_blocking_section_default; */

  this_ctx->caml_force_major_slice = 0;
  this_ctx->caml_signal_handlers = Val_int(0);
  caml_register_global_root_r(this_ctx, &this_ctx->caml_signal_handlers);

  /* from backtrace.c */

#ifdef NATIVE_CODE
  this_ctx->caml_backtrace_active = 0;
  this_ctx->caml_backtrace_pos = 0;
  this_ctx->caml_backtrace_buffer = NULL;
  this_ctx->caml_backtrace_last_exn = Val_unit;
#else
  this_ctx->caml_backtrace_active = 0;
  this_ctx->caml_backtrace_pos = 0;
  this_ctx->caml_backtrace_buffer = NULL;
  this_ctx->caml_backtrace_last_exn = Val_unit;
  this_ctx->caml_cds_file = NULL;
#endif /* #else (#ifdef NATIVE_CODE) */

  /* from compare.c */
  /* this_ctx->compare_stack_init */
  this_ctx->compare_stack = this_ctx->compare_stack_init;
  this_ctx->compare_stack_limit = this_ctx->compare_stack_init + COMPARE_STACK_INIT_SIZE;
  /* this_ctx->caml_compare_unordered; */

  /* from sys.c */
  /* this_ctx->caml_exe_name */
  /* this_ctx->caml_main_argv */

  /* from extern.c */
  /*
  this_ctx->obj_counter;
  this_ctx->size_32;
  this_ctx->size_64;
  this_ctx->extern_ignore_sharing;
  this_ctx->extern_closures;
  this_ctx->extern_cross_context;
  this_ctx->extern_trail_first;
  this_ctx->extern_trail_block;
  this_ctx->extern_trail_cur;
  this_ctx->extern_trail_limit;
  this_ctx->extern_userprovided_output;
  this_ctx->extern_ptr;
  this_ctx->extern_limit;
  this_ctx->extern_output_first;
  this_ctx->extern_output_block;
  this_ctx->extern_stack_init;
  */
  this_ctx->extern_stack = this_ctx->extern_stack_init;
  this_ctx->extern_stack_limit = this_ctx->extern_stack_init + EXTERN_STACK_INIT_SIZE;
  this_ctx->extern_flags[0] = NO_SHARING; this_ctx->extern_flags[1] = CLOSURES; this_ctx->extern_flags[2] = CROSS_CONTEXT;
  
  /* From intext.h */
  /*this_ctx->caml_code_fragments_table;*/

  /* from intern.c */
  /*
  this_ctx->intern_src;
  this_ctx->intern_input;
  this_ctx->intern_input_malloced;
  this_ctx->intern_dest;
  this_ctx->intern_extra_block;
  this_ctx->intern_obj_table;
  this_ctx->intern_color;
  this_ctx->intern_header;
  this_ctx->intern_block;
  */
  this_ctx->camlinternaloo_last_id = NULL;
  /* intern_stack_init[INTERN_STACK_INIT_SIZE]; */
  this_ctx->intern_stack = this_ctx->intern_stack_init;
  this_ctx->intern_stack_limit = this_ctx->intern_stack_init + INTERN_STACK_INIT_SIZE;

  /* from gc_ctrl.c */
  this_ctx->caml_stat_minor_words = 0.0;
  this_ctx->caml_stat_promoted_words = 0.0;
  this_ctx->caml_stat_major_words = 0.0;

  this_ctx->caml_stat_minor_collections = 0;
  this_ctx->caml_stat_major_collections = 0;
  this_ctx->caml_stat_heap_size = 0;
  this_ctx->caml_stat_top_heap_size = 0;
  this_ctx->caml_stat_compactions = 0;
  this_ctx->caml_stat_heap_chunks = 0;
  /* this_ctx->caml_percent_max */

  /* from compact.c */
  /* this_ctx->compact_fl */

  /* from callback.c */
  this_ctx->caml_callback_depth = 0;
  this_ctx->callback_code_threaded = 0;
  int i;
  for(i = 0; i < Named_value_size; i ++)
    this_ctx->named_value_table[i] = NULL;

  /* from debugger.c */
  this_ctx->caml_debugger_in_use = 0;
  /* this_ctx->caml_event_count; */
  this_ctx->caml_debugger_fork_mode = 1;
  this_ctx->marshal_flags = Val_emptylist;

  /* from weak.c */
  this_ctx->caml_weak_list_head = 0;

  /* from finalise.c */
  this_ctx->final_table = NULL;
  this_ctx->final_old = 0;
  this_ctx->final_young = 0;
  this_ctx->final_size = 0;
  this_ctx->to_do_hd = NULL;
  this_ctx->to_do_tl = NULL;
  this_ctx->running_finalisation_function = 0;

  /* from dynlink.c */
  /*
  this_ctx->caml_prim_table
  this_ctx->caml_prim_name_table
  this_ctx->shared_libs;
  this_ctx->caml_shared_libs_path;
  */

  /* from parsing.c */
  this_ctx->caml_parser_trace = 0;

  //caml_context = this_ctx;
  /*
  fprintf(stderr, "set caml_context %x\n", this_ctx);
  fprintf(stderr, "enter_blocking_section_hook = %lx (%lx)\n",
	  & this_ctx->enter_blocking_section_hook,
	  this_ctx->enter_blocking_section_hook);
  fprintf(stderr, "leave_blocking_section_hook = %lx (%lx)\n",
	  & this_ctx->leave_blocking_section_hook,
	  this_ctx->leave_blocking_section_hook);
  fprintf(stderr, "caml_enter_blocking_section_default = %lx\n",
	  caml_enter_blocking_section_default);
  fprintf(stderr, "caml_leave_blocking_section_default = %lx\n",
	  caml_leave_blocking_section_default);
  */

  /* From st_stubs.c */
  this_ctx->all_threads = NULL;
  this_ctx->curr_thread = NULL;
  /* this_ctx->caml_master_lock; */
  this_ctx->caml_tick_thread_running = 0;
  /* this_ctx->caml_tick_thread_id; */
  this_ctx->caml_thread_next_ident = 0;

  /* From scheduler.c: */
  this_ctx->curr_vmthread = NULL;
  this_ctx->next_ident = Val_int(0);
  this_ctx->last_locked_channel = NULL;


  /* Global context-local OCaml variables */
#ifdef NATIVE_CODE
  this_ctx->caml_globals.allocated_size = INITIAL_CAML_GLOBALS_ALLOCATED_SIZE;
  this_ctx->caml_globals.used_size = 0;
  this_ctx->caml_globals.array = caml_stat_alloc(this_ctx->caml_globals.allocated_size);
#endif /* #ifdef NATIVE_CODE */

  /* Global context-local C variables */
  this_ctx->c_globals.allocated_size = INITIAL_C_GLOBALS_ALLOCATED_SIZE;
  this_ctx->c_globals.used_size = 0;
  this_ctx->c_globals.array = caml_stat_alloc(this_ctx->c_globals.allocated_size);

  /* By default, a context is associated with its creating thread: */
  this_ctx->thread = pthread_self();
  caml_set_thread_local_context(this_ctx);

  /* Make a local descriptor for this context: */
  //fprintf(stderr, "Initializing the context descriptor...\n"); fflush(stderr);
  this_ctx->descriptor = caml_stat_alloc(sizeof(struct caml_global_context_descriptor));
  this_ctx->descriptor->kind = caml_global_context_main;
  this_ctx->descriptor->content.local_context.context = this_ctx;
  //fprintf(stderr, "Initialized the context [%p] descriptor [%p]\n", this_ctx, this_ctx->descriptor); fflush(stderr);

  caml_initialize_mutex(&this_ctx->mutex);

  /* We can split in the present state: */
  this_ctx->can_split = 1;

  ///* The main thread is already a user for this context.  This pinning
  //   has to be performed *before* creating the destructor thread, to
  //   ensure the counter is greater than zero when the destructor thread
  //   starts: */
  //caml_pin_context_r(this_ctx);
  //DUMP("added the initial pin");

  /* Context-destructor structures: */
  this_ctx->reference_count = 1; // there is one user thread: the main one
  {CAML_R = this_ctx; DUMP("added the initial pin to the context %p", this_ctx);}
#ifdef HAS_PTHREAD
  caml_initialize_semaphore(&this_ctx->destruction_semaphore, 0);
  int pthread_create_result =
    pthread_create(&this_ctx->destructor_thread, NULL, caml_destructor_thread_function, this_ctx);
  assert(pthread_create_result == 0);
  //caml_initialize_mutex(&this_ctx->reference_count_mutex);
#endif // #ifdef HAS_PTHREAD

  /* The kludgish self-pointer: */
#ifdef HAS_MULTICONTEXT
  this_ctx->ctx = this_ctx;
#endif // #ifdef HAS_MULTICONTEXT
}

caml_global_context *caml_make_first_global_context(void){
#ifdef HAS_MULTICONTEXT
  caml_global_context* the_initial_context_pointer = (caml_global_context*)caml_stat_alloc(sizeof(caml_global_context));
  caml_initialize_first_global_context(the_initial_context_pointer);
  return the_initial_context_pointer;
#else
  static int already_initialized = 0;
  assert(already_initialized == 0);
  caml_initialize_first_global_context(&the_one_and_only_context_struct);
  already_initialized = 1;
  return &the_one_and_only_context_struct;
#endif // #ifdef HAS_MULTICONTEXT
}

#ifdef NATIVE_CODE
/* Return an index, in words */
int caml_allocate_caml_globals_r(CAML_R, size_t added_caml_global_no){
  size_t new_used_size =
    caml_allocate_from_extensible_buffer(&ctx->caml_globals, added_caml_global_no * sizeof(value), /*as the least-significant byte, this yields a non-pointer*/1);
  return new_used_size / sizeof(value);
}
#endif /* #ifdef NATIVE_CODE */

/* Reserve space for a new element of the given size.  Return the new
   object byte offset from the beginning of c_global.array */
static ptrdiff_t caml_allocate_c_global_r(CAML_R, size_t added_bytes){
  /* Add enough padding bytes to make the next element, potentially a
     pointer, at least word-aligned.  This is required on some architectures,
     and yields better performance in any case.  --Luca Saiu */
  size_t old_used_size = (size_t)ctx->c_globals.used_size;
  const size_t word_size = sizeof(void*);
  const size_t old_misalignment = old_used_size % word_size;
  if(old_misalignment != 0)
    ctx->c_globals.used_size += word_size - old_misalignment;

  return caml_allocate_from_extensible_buffer(&ctx->c_globals, added_bytes, 0);
}
caml_c_global_id caml_define_context_local_c_variable_r(CAML_R, size_t added_bytes){
  return caml_allocate_c_global_r(ctx, added_bytes);
}
void* caml_context_local_c_variable_r(CAML_R, caml_c_global_id id){
  return ((char*)ctx->c_globals.array) + id;
}

void caml_scan_caml_globals_r(CAML_R, scanning_action f){
#ifdef NATIVE_CODE
  int i, caml_global_no = ctx->caml_globals.used_size / sizeof(value);
  if(ctx != caml_get_thread_local_context())
    {fprintf(stderr, "Context %p: it's different from the thread-local context %p !!!\n", ctx, caml_get_thread_local_context()); fflush(stderr);};

  /*
  fprintf(stderr, "Context %p: ", ctx);;
  switch(ctx->descriptor->kind){
  case caml_global_context_main:
    fprintf(stderr, "this is the main context.\n"); break;
  case caml_global_context_nonmain_local:
    fprintf(stderr, "this is a non-main local context.\n"); break;
  case caml_global_context_remote:
    fprintf(stderr, "this is a remote context [!!!]\n"); break;
  default:
    fprintf(stderr, "impossible [!!!]\n");
  } // switch
  fflush(stderr);
  */

  if(caml_get_thread_local_context()->descriptor->kind == caml_global_context_nonmain_local){
    if(caml_global_no != 0)
      {/* fprintf(stderr, "Context %p: scanning the %i Caml globals\n", ctx, caml_global_no); fflush(stderr); */}
    else
      {fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~ Context %p [thread %p]: there are no Caml globals to scan [!!!]\n", ctx, (void*)pthread_self()); fflush(stderr);}
  }

  value *caml_globals = (value*)(ctx->caml_globals.array);
  for(i = 0; i < caml_global_no; i ++){
    value *root_pointer = caml_globals + i;
    if(*root_pointer == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th root is zero!\n", ctx, i);
    f(ctx, *root_pointer, root_pointer);
  }
  //printf("Scanning Caml globals: end\n");
#endif /* #ifdef NATIVE_CODE */
}

void (*caml_scan_roots_hook) (scanning_action) = NULL;


/* // FIXME: untyped globals.  Experimental --Luca Saiu REENTRANTRUNTIME */
/* CA___MLprim value caml_make_caml_global_r(CAML_R, value initial_value){ */
/*   value *array = ctx->caml_globals.array; */
/*   int index = caml_allocate_caml_globals_r(ctx, 1); */
/*   array[index] = initial_value; */
/*   return Val_int(index); */
/* } */
/* CA___MLprim value caml_get_caml_global_r(CAML_R, value index){ */
/*   return ((value*)ctx->caml_globals.array)[Long_val(index)]; */
/* } */
/* CA___MLprim value caml_set_caml_global_r(CAML_R, value index, value new_value){ */
/*   ((value*)ctx->caml_globals.array)[Long_val(index)] = new_value; */
/*   return Val_unit; */
/* } */

/* FIXME: these are hooks!  At first I thought they were unimplemented
   stubs.  I think their names are not clear at all, and should
   be changed.  --Luca Saiu REENTRANTRUNTIME*/
void caml_enter_lock_section_default(void)
{
  caml_acquire_global_lock(); // FIXME: experimental --Luca Saiu
}

void caml_leave_lock_section_default(void)
{
  caml_release_global_lock(); // FIXME: experimental --Luca Saiu
}

/* I'm leaving these as globals, shared by all contexts.  --Luca Saiu REENTRANTRUNTIME*/
void (*caml_enter_lock_section_hook)(void) = caml_enter_lock_section_default;
void (*caml_leave_lock_section_hook)(void)= caml_leave_lock_section_default;
void caml_enter_lock_section_r(CAML_R)
{
  caml_enter_blocking_section_r(ctx);
  caml_enter_lock_section_hook();
}
void caml_leave_lock_section_r(CAML_R)
{
  caml_leave_lock_section_hook();
  caml_leave_blocking_section_r(ctx);
}

void (*caml_enter_blocking_section_hook)(void);
void (*caml_leave_blocking_section_hook)(void);
int (*caml_try_leave_blocking_section_hook)(void);

/* the first other context (at pos 0) is always NULL, so that we are sure the first
   line of caml_get_library_context_r is ok to execute. */
static int nbr_other_contexts = 0; // FIXME: I've never touched this, nor library contexts.  Ask Fabrice

library_context *caml_get_library_context_r(CAML_R,
					    int* library_context_pos,
					    int sizeof_library_context,
					    void (*library_context_init_hook)(library_context*)){
  library_context *uctx = (library_context*) ctx->other_contexts[*library_context_pos];
  if(uctx != NULL) return uctx;

  if(*library_context_pos == 0){
    caml_enter_lock_section_r(ctx);
    if(*library_context_pos == 0){

      if(nbr_other_contexts > MAX_OTHER_CONTEXTS){
	caml_leave_lock_section_r(ctx);
	caml_fatal_error("caml_get_other_context: too many other contexts");
	exit(2); /* if needed */
      }
      nbr_other_contexts++;
      //library_context_pos= nbr_other_contexts; // FIXME: GCC emits a warning abouts this line, and GCC seems correct: is there a missing "*"? --Luca Saiu REENTRANTCONTEXT; [this is the version as in the original patch by Fabrice, which produces the warning] // original version
      *library_context_pos= nbr_other_contexts; // FIXME: GCC emits a warning abouts this line, and GCC seems correct: is there a missing "*"? --Luca Saiu REENTRANTCONTEXT; [this is the version as in the original patch by Fabrice, which produces the warning] // fixed version
    }
    caml_leave_lock_section_r(ctx);
  }
  if(uctx == NULL){
    caml_enter_blocking_section_r(ctx);
    uctx = (library_context*) ctx->other_contexts[*library_context_pos];
    if(uctx == NULL){
      uctx = (library_context*)malloc(sizeof_library_context);
      memset(uctx, 0, sizeof_library_context);
      library_context_init_hook(uctx);
      ctx->other_contexts[*library_context_pos] = uctx;
    }
    caml_leave_blocking_section_r(ctx);
  }

  return uctx;
}

void caml_finalize_context_r(CAML_R){
  //fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: OK-1\n", ctx, (void*)(pthread_self())); fflush(stderr);

  caml_destroy_named_value_table_r(ctx);
  caml_remove_global_root_r(ctx, &ctx->caml_signal_handlers);

  //caml_gc_compaction_r(ctx, Val_unit); //!!!!!@@@@@@@@@@@@@@??????????????????
  ///
  /* fprintf(stderr, "Freeing %p\n", ctx->caml_young_base); fflush(stderr); */
  /* free(ctx->caml_young_base); */
  /* fprintf(stderr, "Freeing %p\n", ctx->caml_heap_start); fflush(stderr); */
  /* caml_free_for_heap(ctx->caml_heap_start); */

  /* No global variables are live any more; destroy everything in the Caml heap: */
#ifdef NATIVE_CODE
  caml_shrink_extensible_buffer(&ctx->caml_globals, ctx->caml_globals.used_size);
  //fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: OK-2\n", ctx, (void*)(pthread_self())); fflush(stderr);
  caml_stat_free(ctx->caml_globals.array);
#endif /* #ifdef NATIVE_CODE */

  /* Mark the context as dead in the descriptor, but do *not* free the
     descriptor, which might well be still alive. */
  ctx->descriptor->kind = caml_global_context_dead;
  ctx->descriptor->content.local_context.context = NULL;

  //fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: OK-3\n", ctx, (void*)(pthread_self())); fflush(stderr);
  // Free every dynamically-allocated object which is pointed by the context data structure [FIXME: really do it]:
  //fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: FIXME: actually free everything\n", ctx, (void*)(pthread_self())); fflush(stderr);

  //fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: OK-4\n", ctx, (void*)(pthread_self())); fflush(stderr);
  fprintf(stderr, "caml_finalize_context_r [context %p] [thread %p]: OK-5: finalized %p\n", ctx, (void*)(pthread_self()), ctx); fflush(stderr);
  // FIXME: really destroy stuff
}

#ifdef HAS_MULTICONTEXT
void caml_destroy_context_r(CAML_R){
  caml_finalize_context_r(ctx);

  /* Free the context data structure ifself: */
  caml_stat_free(ctx);
}
#endif // #ifdef HAS_MULTICONTEXT

#ifdef NATIVE_CODE
/* The index of the first word in caml_globals which is not used yet.
   This variable is shared by all contexts, and accessed in mutual
   exclusion. */
static long first_unused_word_offset = 0;

void caml_register_module_r(CAML_R, size_t size_in_bytes, long *offset_pointer){
  /* Compute the size in words, which is to say how many globals are there: */
  int size_in_words = size_in_bytes / sizeof(void*);
  /* We keep the module name right after the offset pointer, as a read-only string: */
  char *module_name __attribute__((unused)) = (char*)offset_pointer + sizeof(long);
  DUMP("module_name is %s (%li bytes); offset_pointer is at %p", module_name, (long)size_in_bytes, offset_pointer);
  Assert(size_in_words * sizeof(void*) == size_in_bytes); /* there's a whole number of globals */
  //fprintf(stderr, "caml_register_module_r [context %p]: registering %s%p [%lu bytes at %p]: BEGIN\n", ctx, module_name, offset_pointer, (unsigned long)size_in_bytes, offset_pointer); fflush(stderr);

  /* If this is the first time we register this module, make space for its globals in
     ctx->caml_globals.  If the module was already registered, do nothing. */
  caml_acquire_global_lock();
  if(*offset_pointer == -1){
    /* fprintf(stderr, "Registering the module %p for the first time: making place for %i globals\n", offset_pointer, (int)size_in_words); */
    /* fprintf(stderr, "first_unused_word_offset is %i\n", (int)first_unused_word_offset); */
    *offset_pointer = first_unused_word_offset * sizeof(void*);
    /* fprintf(stderr, "We have set the offset for the module %p to %i bytes (%i words)\n", offset_pointer, (int)*offset_pointer, (int)(*offset_pointer / sizeof(void*))); */
    first_unused_word_offset += size_in_words;
    caml_allocate_caml_globals_r(ctx, size_in_words);
    //caml_resize_global_array_r(ctx, first_unused_word_offset);
    /* fprintf(stderr, "The new first_unused_word_offset is %i\n", (int)first_unused_word_offset); */
    /* fprintf(stderr, "The global vector is now at %p\n", (void*)ctx->caml_globals.array); */
  }
  /* else */
  /*   fprintf(stderr, "The module %p has already been registered: its offset is %i\n", offset_pointer, (int)*offset_pointer); */
  caml_release_global_lock();
  /* fprintf(stderr, "The offset (in bytes) we just wrote at %p is %li\n", offset_pointer, *offset_pointer); */
  /* fprintf(stderr, "The context is at %p\n", (void*)ctx); */
  /* fprintf(stderr, "Globals are at %p\n", (void*)ctx->caml_globals.array); */
  //fprintf(stderr, "caml_register_module_r [context %p]: registered %s@%p.  END (still alive)\n", ctx, module_name, offset_pointer); fflush(stderr);
}
#endif /* #ifdef NATIVE_CODE */

void caml_after_module_initialization_r(CAML_R, size_t size_in_bytes, long *offset_pointer){
  /* We keep the module name right after the offset pointer, as a read-only string: */
  char *module_name __attribute__ (( unused )) = (char*)offset_pointer + sizeof(long);
  //fprintf(stderr, "caml_after_module_initialization_r [context %p]: %s@%p: still alive.\n", ctx, module_name, offset_pointer); fflush(stderr);
  /*
  fprintf(stderr, "caml_after_module_initialization_r: BEGIN [%lu bytes at %p]\n",
         (unsigned long)size_in_bytes,
         offset_pointer);
  void **p;
  void **limit = (void**)(((char*)offset_pointer) + size_in_bytes);
  int i;
  for(p = (void**)offset_pointer, i = 0; i < 1; p ++, i ++){
    void *word = *p;
    fprintf(stderr, ".text word #%i: %p %lu %li\n", i, word, (unsigned long)word, (long)word);
  } // for
  long offset_in_bytes = *offset_pointer;
  long offset_in_words = offset_in_bytes / sizeof(void*);
  for(p = ((void**)ctx->caml_globals.array) + offset_in_words, i = 0; i < (size_in_bytes / sizeof(void*)); p ++, i ++){
    void *word = *p;
    fprintf(stderr, "Global Word #%i: %p %lu %li\n", i, word, (unsigned long)word, (long)word);
  } // for
  fprintf(stderr, "The offset (in bytes) at %p is %li\n", offset_pointer, offset_in_bytes);
  fprintf(stderr, "There exist %i globals in this context\n", (int)ctx->caml_globals.used_size / sizeof(void*));
  fprintf(stderr, "caml_after_module_initialization_r: END (still alive)\n\n");
  */
}

/* FIXME: use a custom value instead.  However this in practice works
   fine on 64-bit architectures: */
value caml_value_of_mailbox(struct caml_mailbox *c){
  return Val_long((long)c);
}
struct caml_mailbox* caml_mailbox_of_value(value l){
  return (struct caml_mailbox*)(Long_val(l));
}

/* FIXME: use a custom value instead.  However this in practice works
   fine on 64-bit architectures: */
value caml_value_of_context_descriptor(struct caml_global_context_descriptor *c){
  return Val_long((long)c);
}
struct caml_global_context_descriptor* caml_global_context_descriptor_of_value(value l){
  return (struct caml_global_context_descriptor*)(Long_val(l));
}

CAMLprim value caml_context_self_r(CAML_R)
{
  return caml_value_of_context_descriptor(ctx->descriptor);
}

CAMLprim value caml_context_is_main_r(CAML_R, value descriptor)
{
  //fprintf(stderr, "caml_context_is_main_r [context %p] [thread %p]: the result is %i\n", ctx, (void*)(pthread_self()), caml_global_context_descriptor_of_value(descriptor)->kind == caml_global_context_main); fflush(stderr);
  return Val_bool(caml_global_context_descriptor_of_value(descriptor)->kind == caml_global_context_main);
}

CAMLprim value caml_context_is_alive_r(CAML_R, value descriptor)
{
  //fprintf(stderr, "caml_context_is_main_r [context %p] [thread %p]: the result is %i\n", ctx, (void*)(pthread_self()), caml_global_context_descriptor_of_value(descriptor)->kind == caml_global_context_main); fflush(stderr);
  return Val_bool(caml_global_context_descriptor_of_value(descriptor)->kind == caml_global_context_dead);
}

CAMLprim value caml_context_is_remote_r(CAML_R, value descriptor)
{
  return Val_bool(caml_global_context_descriptor_of_value(descriptor)->kind == caml_global_context_remote);
}

CAMLprim value caml_cpu_no_r(CAML_R, value unit){
  /* FIXME: this is a GNU extension.  What should we do on non-GNU systems? */
  int cpu_no =
    //get_nprocs_conf();
    sysconf(_SC_NPROCESSORS_ONLN);
  return Val_int(cpu_no);
}

CAMLprim value caml_set_debugging(value bool){
  caml_debugging = Int_val(bool);
  return Val_unit;
}

void caml_context_initialize_global_stuff(void){
  /* Attempt to prevent multiple initialization.  This will not be
     100% reliable in particularly perverse cases which would require
     synchronization: we can't use the global mutex, since we're gonna
     initialize it here. */
  if(caml_are_mutexes_already_initialized){
    fprintf(stderr, "caml_initialize_global_stuff: called more than once\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
  }
  caml_are_mutexes_already_initialized = 1;

  caml_enter_blocking_section_hook = &caml_enter_blocking_section_default;
  caml_leave_blocking_section_hook = &caml_leave_blocking_section_default;
  caml_try_leave_blocking_section_hook = &caml_try_leave_blocking_section_default;

  /* Create global locks: */
  caml_initialize_mutex(&caml_global_mutex);
  caml_initialize_mutex(&caml_channel_mutex);
  //caml_are_global_mutexes_initialized = 1;
}


/* This is a thin wrapper over pthread_mutex_lock and pthread_mutex_unlock: */
static void caml_call_on_mutex(int(*function)(pthread_mutex_t *), pthread_mutex_t *mutex){
  INIT_CAML_R;
  if(! caml_are_mutexes_already_initialized){
    /* INIT_CAML_R; */ fprintf(stderr, "global mutexes aren't initialized yet.  Bailing out"); fflush(stderr);
    exit(EXIT_FAILURE);
  }
  int result __attribute__((unused));
  result = function(mutex);
  if(result){
    fprintf(stderr, "the function %p failed on the mutex %p", function, mutex); fflush(stderr);
    exit(EXIT_FAILURE);
  }
  //Assert(result == 0);
}

/* void caml_acquire_global_lock(void){ */
/*   INIT_CAML_R; */
/*   if(! caml_are_mutexes_already_initialized){ */
/*     /\* INIT_CAML_R; *\/ DUMP("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ caml_global_mutex is not yet initialized"); */
/*     return; */
/*   } */

/*   /\* FIXME: is this needed?  I wanna play it safe --Luca Saiu REENTRANTRUNTIME *\/ */
/*   //int old_value = caml_global_mutex.__data.__count; */
/*   //int old_owner = caml_global_mutex.__data.__owner; */
/*   int result __attribute__((unused)); */
/*   //DUMP("lock"); */
/*   result = pthread_mutex_lock(&caml_global_mutex); */
/*   /////BEGIN */
/*   if(result){ */
/*     DUMP("thread_mutex_lock failed"); */
/*     exit(EXIT_FAILURE); */
/*   } */
/*   //fprintf(stderr, "+[context %p] {%u %p->%u %p | %p}\n", ctx, old_value, (void*)(long)old_owner, caml_global_mutex.__data.__count, (void*)(long)caml_global_mutex.__data.__owner, (void*)(pthread_self())); fflush(stderr); */
/*   /////END */
/*   Assert(result == 0); */
/* } */

/* void caml_release_global_lock(void){ */
/*   if(! caml_are_mutexes_already_initialized){ */
/*     INIT_CAML_R; DUMP("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ caml_global_mutex is not yet initialized"); */
/*     return; */
/*   } */

/*   //int old_value = caml_global_mutex.__data.__count; */
/*   //int old_owner = caml_global_mutex.__data.__owner; */
/*   INIT_CAML_R; */
/*   int result __attribute__((unused)) = pthread_mutex_unlock(&caml_global_mutex); */
/*   //DUMP("unlock"); */
/*   Assert(result == 0); */
/*   /////BEGIN */
/*   if(result){ */
/*     DUMP("pthread_mutex_unlock failed"); */
/*     //volatile int a = 1; a /= 0; */
/*     exit(EXIT_FAILURE); */
/*   } */
/*   //fprintf(stderr, "-[context %p] {%u %p->%u %p | %p}\n", ctx, old_value, (void*)(long)old_owner, caml_global_mutex.__data.__count, (void*)(long)caml_global_mutex.__data.__owner, (void*)(pthread_self())); fflush(stderr); */
/*   /////END */
/* } */


void caml_acquire_global_lock(void){
  caml_call_on_mutex(pthread_mutex_lock, &caml_global_mutex);
}
void caml_release_global_lock(void){
  caml_call_on_mutex(pthread_mutex_unlock, &caml_global_mutex);
}

// TEMPORARY SCREWUP: BEGIN !!!!!!!!!!!!!!!!!!!!!!!!!
static pthread_t *current_owner = NULL;
void caml_acquire_channel_lock(void){
  /* INIT_CAML_R; */
  /* int trylock_result = */
  /*   pthread_mutex_trylock(&caml_channel_mutex); */
  /* if(trylock_result == EBUSY){ */
  /*   DUMP("pthread_mutex_trylock: got EBUSY"); */
  /*   caml_enter_blocking_section_r(ctx); */
  /*   DUMP("after entering blocking section"); */
    caml_call_on_mutex(pthread_mutex_lock, &caml_channel_mutex);
  /*   DUMP("pthread_mutex_lock'ed, after getting EBUSY from pthread_mutex_trylock"); */
  /*   caml_leave_blocking_section_r(ctx); */
  /*   DUMP("after leaving blocking section"); */
  /* } */
  /* else{ */
  /*   DUMP("pthread_mutex_trylock: locked"); */
  /* } */
  /* Assert(trylock_result != EINVAL); */
  /* current_owner = pthread_self(); */
  //INIT_CAML_R; caml_enter_blocking_section_r(ctx);
  //  caml_call_on_mutex(pthread_mutex_lock, &caml_channel_mutex);
  //caml_leave_blocking_section_r(ctx);
}
void caml_release_channel_lock(void){
  /* current_owner = NULL; */
  /* INIT_CAML_R; DUMP("pthread_mutex_trylock: unlocking"); */
  caml_call_on_mutex(pthread_mutex_unlock, &caml_channel_mutex);
  /* DUMP("pthread_mutex_trylock: unlocked"); */
}
// TEMPORARY SCREWUP: END !!!!!!!!!!!!!!!!!!!!!!!!!

void caml_acquire_contextual_lock(CAML_R){
  caml_call_on_mutex(pthread_mutex_lock, &ctx->mutex);
  //int result = pthread_mutex_lock(&ctx->mutex);
  //assert(result == 0);
}
void caml_release_contextual_lock(CAML_R){
  caml_call_on_mutex(pthread_mutex_unlock, &ctx->mutex);
  //int result = pthread_mutex_unlock(&ctx->mutex);
  //assert(result == 0);
}


/* void caml_dump_global_mutex(void){ */
/*   fprintf(stderr, "{%u %p | %p}\n", caml_global_mutex.__data.__count, (void*)(long)caml_global_mutex.__data.__owner, (void*)(pthread_self())); fflush(stderr); */
/* } */

//#ifndef NATIVE_CODE //FIXME: remove later.  This is for debugging only
//#endif // #ifndef NATIVE_CODE

/* /\* Return the number of threads associated to the given context: *\/ */
/* static int (*the_caml_get_thread_no_r)(CAML_R) = NULL; */
/* void caml_set_caml_get_thread_no_r(CAML_R, int (*f)(CAML_R)){ */
/*   the_caml_get_thread_no_r = f; */
/* } */

/* int caml_get_thread_no_r(CAML_R){ */
/*   if(the_caml_get_thread_no_r != NULL) */
/*     return the_caml_get_thread_no_r(ctx); */
/*   else */
/*     return -1; */
/* } */


static void (*the_caml_initialize_context_thread_support_r)(CAML_R) = NULL;
void caml_set_caml_initialize_context_thread_support_r(void (*f)(CAML_R)){
  the_caml_initialize_context_thread_support_r = f;
}

void caml_initialize_context_thread_support_r(CAML_R){
  if(the_caml_initialize_context_thread_support_r != NULL)
    the_caml_initialize_context_thread_support_r(ctx);
}

int caml_can_split_r(CAML_R){
  return ctx->can_split;
}

void caml_pin_context_r(CAML_R){
  Assert(ctx->reference_count > 0);
  ctx->reference_count ++;
  DUMP("  PIN %i -> %i", ctx->reference_count - 1, ctx->reference_count);
}

void caml_unpin_context_r(CAML_R){
  Assert(ctx->reference_count > 0);
  ctx->reference_count --;
  DUMP("UNpin %i -> %i", ctx->reference_count + 1, ctx->reference_count);
  if(ctx->reference_count == 0){
    DUMP("removed the last pin");
#ifdef HAS_MULTICONTEXT
    caml_v_semaphore(&ctx->destruction_semaphore);
#else
    caml_run_at_context_exit_functions_r(&the_one_and_only_context_struct);
    caml_finalize_context_r(&the_one_and_only_context_struct);
#endif // #ifdef HAS_MULTICONTEXT
    /* if(caml_remove_last_pin_from_context_hook != NULL) */
    /*   caml_remove_last_pin_from_context_hook(ctx); */
  }
}
CAMLprim value caml_unpin_context_primitive_r(CAML_R, value unit){
  DUMP("explicitly unpinning");
  caml_unpin_context_r(ctx);
  return Val_unit;
}

/* static void caml_default_remove_last_pin_from_context_hook_r(CAML_R){ */
/*   caml_destroy_context_r(ctx); */
/* } */
/* void (*caml_remove_last_pin_from_context_hook)(CAML_R) = caml_default_remove_last_pin_from_context_hook_r; */



/* CAMLprim int caml_multi_context_implemented(value unit){ */
/* #if HAS_MULTICONTEXT */
/*   return Bool_val(1); */
/* #else */
/*   return Bool_val(0); */
/* #endif /\* #if HAS_MULTICONTEXT *\/ */
/* } */

__thread int caml_indentation_level = 0; // FIXME: remove this crap after debugging !!!!!!!!!!!!!!!!

// FIXME: remove this kludge

int TRIVIAL_caml_systhreads_get_thread_no_r(CAML_R){
  return 0;
}
int caml_systhreads_get_thread_no_r (CAML_R) __attribute__ ((weak, alias ("TRIVIAL_caml_systhreads_get_thread_no_r")));

int caml_debugging = 0; // !!!!!!!!!!!!!!!!!!!!!!!
