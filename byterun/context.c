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
#include <pthread.h>
#include <errno.h> // for EBUSY.  FIXME: ensure this is still needed at the end --Luca Saiu REENTRANTRUNTIME

static __thread caml_global_context *the_thread_local_caml_context = NULL;

/* The one and only main context: */
caml_global_context *the_main_context = NULL;

caml_global_context *caml_get_thread_local_context(void)
{
  return the_thread_local_caml_context;
}

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
}

void caml_finalize_mutex(pthread_mutex_t *mutex){
  pthread_mutex_destroy(mutex);
}

void caml_initialize_semaphore(sem_t *semaphore, int initial_value){
  int init_result = sem_init(semaphore, /*not process-shared*/0, initial_value);
  if(init_result != 0){
    fprintf(stderr, "++++++++ [thread %p] sem_init failed\n", (void*)(pthread_self())); fflush(stderr);
    exit(EXIT_FAILURE);
  }
}
void caml_finalize_semaphore(sem_t *semaphore){
  sem_destroy(semaphore);
}

void caml_p_semaphore(sem_t* semaphore){
  int sem_wait_result;
  while((sem_wait_result = sem_wait(semaphore)) != 0){
    assert(errno == EINTR);
    INIT_CAML_R; DUMP("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! sem_wait was interrupted by a signal");
    errno = 0;
  }
  assert(sem_wait_result == 0);
}
void caml_v_semaphore(sem_t* semaphore){
  int sem_post_result = sem_post(semaphore);
  assert(sem_post_result == 0);
}

void* caml_destructor_thread_function(void *ctx_as_void_star){
  CAML_R = ctx_as_void_star;

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

caml_global_context *caml_initialize_first_global_context(void)
{
  /* Maybe we should use partial contexts for specific tasks, that
will probably not be used by all threads.  We should check the size of
each part of the context, to allocate only what is probably required
by all threads, and then allocate other sub-contexts on demand. */

  caml_global_context* ctx = (caml_global_context*)caml_stat_alloc(sizeof(caml_global_context));
  /*
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT  --Luca Saiu REENTRANTRUNTIME: BEGIN
FIXME: This is a pretty bad symptom.  If I replace the 0 with a 1, the
thing always crashes; but the memset call was just there for
defensiveness, [I suppose, actually, the memset call has been there
since the original version by Fabrice]... There is some struct field
which is never correctly initialized.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT  --Luca Saiu REENTRANTRUNTIME: END
*/
  //memset(ctx, 1, sizeof(caml_global_context));
  //memset(ctx, -1, sizeof(caml_global_context));
  memset(ctx, 0, sizeof(caml_global_context));

#ifdef NATIVE_CODE
  /* TODO : only for first context ! We should implement a way, so
that calling several times caml_main will actually start several
runtimes, with different contexts. A thread would then be able to
schedule several ocaml runtimes ! Protect this with a protected
section.  */
  ctx->caml_globals_map = caml_globals_map; // FIXME: this is Fabrice's version; I really have no reason to change it, except to see the effect --Luca Saiu REENTRANTRUNTIME
  //ctx->caml_globals_map = NULL; // FIXME: horrible, horrible test.  I'm intentionally breaking Fabrice's code to see what breaks [nothing, apparently].  --Luca Saiu REENTRANTRUNTIME
#endif/* #ifdef NATIVE_CODE */

  /* from stacks.c */
  /*  value caml_global_data; */
  ctx->caml_stack_usage_hook = NULL;
  /*  ctx->caml_stack_low;
  ctx->caml_stack_high;
  ctx->caml_stack_threshold;
  ctx->caml_extern_sp;
  ctx->caml_trapsp;
  ctx->caml_trap_barrier;
  ctx->caml_max_stack_size;
  */

  /* from major_gc.c */
  /*  ctx->caml_percent_free;
  ctx->caml_major_heap_increment;
  ctx->caml_heap_start;
  ctx->caml_gc_sweep_hp;
  ctx->caml_gc_phase;
  ctx->gray_vals;
  ctx->gray_vals_cur;
  ctx->gray_vals_end;
  ctx->gray_vals_size;
  ctx->heap_is_pure;
  ctx->caml_allocated_words;
  ctx->caml_dependent_size;
  ctx->caml_dependent_allocated;
  ctx->caml_extra_heap_resources;
*/
  ctx->caml_fl_size_at_phase_change = 0;
  /*
  ctx->markhp;
  ctx->mark_chunk;
  ctx->mark_limit;
  ctx->caml_gc_subphase;
  ctx->weak_prev;
  */
#ifdef DEBUG
  ctx->major_gc_counter = 0;
#endif

  /* from freelist.c */
  ctx->sentinel.filler1 = 0;
  ctx->sentinel.h = Make_header (0, 0, Caml_blue);
  ctx->sentinel.first_bp = 0;
  ctx->sentinel.filler2 = 0;
#define Fl_head ((char *) (&(ctx->sentinel.first_bp)))
  ctx->fl_prev = Fl_head;
  ctx->fl_last = NULL;
  ctx->caml_fl_merge = Fl_head;
  ctx->caml_fl_cur_size = 0;
  /*  ctx->last_fragment; */
  /*  ctx->flp [FLP_MAX]; */
  ctx->flp_size = 0;
  ctx->beyond = NULL;
  ctx->caml_allocation_policy = Policy_next_fit;

  /* from minor_gc.c */
  /*  ctx->caml_minor_heap_size; */
  ctx->caml_young_base = NULL;
  ctx->caml_young_start = NULL;
  ctx->caml_young_end = NULL;
  ctx->caml_young_ptr = NULL;
  ctx->caml_young_limit = NULL;

  ctx->caml_ref_table.base = NULL;
  ctx->caml_ref_table.end = NULL;
  ctx->caml_ref_table.threshold = NULL;
  ctx->caml_ref_table.ptr = NULL;
  ctx->caml_ref_table.limit = NULL;
  ctx->caml_ref_table.size = 0;
  ctx->caml_ref_table.reserve = 0;

  ctx->caml_weak_ref_table.base = NULL;
  ctx->caml_weak_ref_table.end = NULL;
  ctx->caml_weak_ref_table.threshold = NULL;
  ctx->caml_weak_ref_table.ptr = NULL;
  ctx->caml_weak_ref_table.limit = NULL;
  ctx->caml_weak_ref_table.size = 0;
  ctx->caml_weak_ref_table.reserve = 0;
  ctx->caml_in_minor_collection = 0;
  ctx->oldify_todo_list = 0;
#ifdef DEBUG
  ctx->minor_gc_counter = 0;
#endif

#if 0
  /* from memory.h */
#ifdef ARCH_SIXTYFOUR
  /* ctx->caml_page_table */
#else
  /*  ctx->caml_page_table[Pagetable1_size]; */
  ctx->caml_page_table_empty[0] = 0;
#endif/* #else (#ifdef ARCH_SIXTYFOUR) */
#endif/* #if 0 */

  /* from roots.c */
#ifdef NATIVE_CODE
  ctx->caml_local_roots = NULL;
  //ctx->caml_scan_roots_hook = NULL;
  /* Fabrice's original version; see my comment in context.h --Luca Saiu REENTRANTRUNTIME */
  /*  ctx->caml_top_of_stack; */
  /* ctx->caml_bottom_of_stack = NULL; */
  /* ctx->caml_last_return_address = 1; */
  /* /\*  ctx->caml_gc_regs; */
  /*     ctx->caml_globals_inited; *\/ */
  /* ctx->caml_globals_scanned = 0; */
  /* ctx->caml_dyn_globals = NULL; */
  /* ctx->caml_top_of_stack; */
  ctx->caml_bottom_of_stack = NULL; /* no stack initially */
  ctx->caml_last_return_address = 1; /* not in OCaml code initially */
  /* ctx->caml_gc_regs; */
  ctx->caml_globals_inited = 0;
  ctx->caml_globals_scanned = 0;
  ctx->caml_dyn_globals = NULL;
  ctx->caml_stack_usage_hook = NULL;
#else
  ctx->caml_local_roots = NULL;
  //ctx->caml_scan_roots_hook = NULL;
#endif/* #else (#ifdef NATIVE_CODE) */


#ifdef CAML_CONTEXT_STARTUP
  /* from startup.c */
#ifdef NATIVE_CODE
  /* ctx->caml_atom_table
     ctx->caml_code_area_start
     ctx->caml_code_area_end */
  /*  ctx->caml_termination_jmpbuf */
  ctx->caml_termination_hook = NULL;
#endif/* #ifdef NATIVE_CODE */
#endif/* #ifdef CAML_CONTEXT_STARTUP */

  /* from globroots.c */
  ctx->random_seed = 0;

  ctx->caml_global_roots.root = NULL;
  ctx->caml_global_roots.forward[0] = NULL;
  ctx->caml_global_roots.level = 0;
  
  ctx->caml_global_roots_young.root = NULL;
  ctx->caml_global_roots_young.forward[0] = NULL;
  ctx->caml_global_roots_young.level = 0;
  
  ctx->caml_global_roots_old.root = NULL;
  ctx->caml_global_roots_old.forward[0] = NULL;
  ctx->caml_global_roots_old.level = 0;
  
  /* from fail.c */
#ifdef NATIVE_CODE
  ctx->caml_exception_pointer= NULL;
  ctx->array_bound_error_bucket_inited = 0;
#else
  ctx->caml_external_raise = NULL;
  /* ctx->caml_exn_bucket */
  ctx->out_of_memory_bucket.hdr = 0; ctx->out_of_memory_bucket.exn = 0;
#endif /* #else (#ifdef NATIVE_CODE) */

  /* from signals_byt.c */
  ctx->caml_something_to_do = 0;
  ctx->caml_async_action_hook = NULL;

  /* from signals.c */
  ctx->caml_signals_are_pending = 0;
  /* ctx->caml_pending_signals */
  ctx->caml_async_signal_mode = 0;

  /* ctx->caml_enter_blocking_section_hook = &caml_enter_blocking_section_default; */
  /* ctx->caml_leave_blocking_section_hook = &caml_leave_blocking_section_default; */
  /* ctx->caml_try_leave_blocking_section_hook = &caml_try_leave_blocking_section_default; */

  ctx->caml_force_major_slice = 0;
  ctx->caml_signal_handlers = Val_int(0);
  caml_register_global_root_r(ctx, &ctx->caml_signal_handlers);

  /* from backtrace.c */

#ifdef NATIVE_CODE
  ctx->caml_backtrace_active = 0;
  ctx->caml_backtrace_pos = 0;
  ctx->caml_backtrace_buffer = NULL;
  ctx->caml_backtrace_last_exn = Val_unit;
#else
  ctx->caml_backtrace_active = 0;
  ctx->caml_backtrace_pos = 0;
  ctx->caml_backtrace_buffer = NULL;
  ctx->caml_backtrace_last_exn = Val_unit;
  ctx->caml_cds_file = NULL;
#endif /* #else (#ifdef NATIVE_CODE) */

  /* from compare.c */
  /* ctx->compare_stack_init */
  ctx->compare_stack = ctx->compare_stack_init;
  ctx->compare_stack_limit = ctx->compare_stack_init + COMPARE_STACK_INIT_SIZE;
  /* ctx->caml_compare_unordered; */

  /* from sys.c */
  /* ctx->caml_exe_name */
  /* ctx->caml_main_argv */

  /* from extern.c */
  /*
  ctx->obj_counter;
  ctx->size_32;
  ctx->size_64;
  ctx->extern_ignore_sharing;
  ctx->extern_closures;
  ctx->extern_cross_context;
  ctx->extern_trail_first;
  ctx->extern_trail_block;
  ctx->extern_trail_cur;
  ctx->extern_trail_limit;
  ctx->extern_userprovided_output;
  ctx->extern_ptr;
  ctx->extern_limit;
  ctx->extern_output_first;
  ctx->extern_output_block;
  ctx->extern_stack_init;
  */
  ctx->extern_stack = ctx->extern_stack_init;
  ctx->extern_stack_limit = ctx->extern_stack_init + EXTERN_STACK_INIT_SIZE;
  ctx->extern_flags[0] = NO_SHARING; ctx->extern_flags[1] = CLOSURES; ctx->extern_flags[2] = CROSS_CONTEXT;
  
  /* From intext.h */
  /*ctx->caml_code_fragments_table;*/

  /* from intern.c */
  /*
  ctx->intern_src;
  ctx->intern_input;
  ctx->intern_input_malloced;
  ctx->intern_dest;
  ctx->intern_extra_block;
  ctx->intern_obj_table;
  ctx->intern_color;
  ctx->intern_header;
  ctx->intern_block;
  */
  ctx->camlinternaloo_last_id = NULL;
  /* intern_stack_init[INTERN_STACK_INIT_SIZE]; */
  ctx->intern_stack = ctx->intern_stack_init;
  ctx->intern_stack_limit = ctx->intern_stack_init + INTERN_STACK_INIT_SIZE;

  /* from gc_ctrl.c */
  ctx->caml_stat_minor_words = 0.0;
  ctx->caml_stat_promoted_words = 0.0;
  ctx->caml_stat_major_words = 0.0;

  ctx->caml_stat_minor_collections = 0;
  ctx->caml_stat_major_collections = 0;
  ctx->caml_stat_heap_size = 0;
  ctx->caml_stat_top_heap_size = 0;
  ctx->caml_stat_compactions = 0;
  ctx->caml_stat_heap_chunks = 0;
  /* ctx->caml_percent_max */

  /* from compact.c */
  /* ctx->compact_fl */

  /* from callback.c */
  ctx->caml_callback_depth = 0;
  ctx->callback_code_threaded = 0;
  int i;
  for(i = 0; i < Named_value_size; i ++)
    ctx->named_value_table[i] = NULL;

  /* from debugger.c */
  ctx->caml_debugger_in_use = 0;
  /* ctx->caml_event_count; */
  ctx->caml_debugger_fork_mode = 1;
  ctx->marshal_flags = Val_emptylist;

  /* from weak.c */
  ctx->caml_weak_list_head = 0;

  /* from finalise.c */
  ctx->final_table = NULL;
  ctx->final_old = 0;
  ctx->final_young = 0;
  ctx->final_size = 0;
  ctx->to_do_hd = NULL;
  ctx->to_do_tl = NULL;
  ctx->running_finalisation_function = 0;

  /* from dynlink.c */
  /*
  ctx->caml_prim_table
  ctx->caml_prim_name_table
  ctx->shared_libs;
  ctx->caml_shared_libs_path;
  */

  /* from parsing.c */
  ctx->caml_parser_trace = 0;

  //caml_context = ctx;
  /*
  fprintf(stderr, "set caml_context %x\n", ctx);
  fprintf(stderr, "enter_blocking_section_hook = %lx (%lx)\n",
	  & ctx->enter_blocking_section_hook,
	  ctx->enter_blocking_section_hook);
  fprintf(stderr, "leave_blocking_section_hook = %lx (%lx)\n",
	  & ctx->leave_blocking_section_hook,
	  ctx->leave_blocking_section_hook);
  fprintf(stderr, "caml_enter_blocking_section_default = %lx\n",
	  caml_enter_blocking_section_default);
  fprintf(stderr, "caml_leave_blocking_section_default = %lx\n",
	  caml_leave_blocking_section_default);
  */

  /* From st_stubs.c */
  ctx->all_threads = NULL;
  ctx->curr_thread = NULL;
  /* ctx->caml_master_lock; */
  ctx->caml_tick_thread_running = 0;
  /* ctx->caml_tick_thread_id; */
  ctx->caml_thread_next_ident = 0;

  /* From scheduler.c: */
  ctx->curr_vmthread = NULL;
  ctx->next_ident = Val_int(0);
  ctx->last_locked_channel = NULL;


  /* Global context-local OCaml variables */
#ifdef NATIVE_CODE
  ctx->caml_globals.allocated_size = INITIAL_CAML_GLOBALS_ALLOCATED_SIZE;
  ctx->caml_globals.used_size = 0;
  ctx->caml_globals.array = caml_stat_alloc(ctx->caml_globals.allocated_size);
#endif /* #ifdef NATIVE_CODE */

  /* Global context-local C variables */
  ctx->c_globals.allocated_size = INITIAL_C_GLOBALS_ALLOCATED_SIZE;
  ctx->c_globals.used_size = 0;
  ctx->c_globals.array = caml_stat_alloc(ctx->c_globals.allocated_size);

  /* By default, a context is associated with its creating thread: */
  ctx->thread = pthread_self();
  caml_set_thread_local_context(ctx);

  /* Make a local descriptor for this context: */
  //fprintf(stderr, "Initializing the context descriptor...\n"); fflush(stderr);
  ctx->descriptor = caml_stat_alloc(sizeof(struct caml_global_context_descriptor));
  ctx->descriptor->kind = caml_global_context_main;
  ctx->descriptor->content.local_context.context = ctx;
  //fprintf(stderr, "Initialized the context [%p] descriptor [%p]\n", ctx, ctx->descriptor); fflush(stderr);

  caml_initialize_mutex(&ctx->mutex);

  /* We can split in the present state: */
  ctx->can_split = 1;

  /* Context-destructor structures: */
  ctx->reference_count = 0;
  caml_initialize_semaphore(&ctx->destruction_semaphore, 0);
  int pthread_create_result = pthread_create(&ctx->destructor_thread, NULL, caml_destructor_thread_function, ctx);
  assert(pthread_create_result == 0);
  //caml_initialize_mutex(&ctx->reference_count_mutex);

  /* The kludgish self-pointer: */
  ctx->ctx = ctx;

  /* The main thread is already a user for this context: */
  caml_pin_context_r(ctx);

  return ctx;
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

extern void caml_destroy_context_r(CAML_R){
  //fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: OK-1\n", ctx, (void*)(pthread_self())); fflush(stderr);

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
  //fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: OK-2\n", ctx, (void*)(pthread_self())); fflush(stderr);
  caml_stat_free(ctx->caml_globals.array);
#endif /* #ifdef NATIVE_CODE */

  /* Mark the context as dead in the descriptor, but do *not* free the
     descriptor, which might well be still alive. */
  ctx->descriptor->kind = caml_global_context_dead;
  ctx->descriptor->content.local_context.context = NULL;

  //fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: OK-3\n", ctx, (void*)(pthread_self())); fflush(stderr);
  // Free every dynamically-allocated object which is pointed by the context data structure [FIXME: really do it]:
  //fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: FIXME: actually free everything\n", ctx, (void*)(pthread_self())); fflush(stderr);

  //fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: OK-4\n", ctx, (void*)(pthread_self())); fflush(stderr);
  /* Free the context data structure ifself: */
  caml_stat_free(ctx);
  fprintf(stderr, "caml_destroy_context_r [context %p] [thread %p]: OK-5: destroyed %p\n", ctx, (void*)(pthread_self()), ctx); fflush(stderr);
  // FIXME: really destroy stuff
}

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
  /* Attempt to prevent multiple initialization.  This will not always
     work, because of missing synchronization: we can't use the global
     mutex, since we're gonna initialize it here. */
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


void caml_dump_global_mutex(void){
  fprintf(stderr, "{%u %p | %p}\n", caml_global_mutex.__data.__count, (void*)(long)caml_global_mutex.__data.__owner, (void*)(pthread_self())); fflush(stderr);
}

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
    caml_v_semaphore(&ctx->destruction_semaphore);
    /* if(caml_remove_last_pin_from_context_hook != NULL) */
    /*   caml_remove_last_pin_from_context_hook(ctx); */
  }
}

/* static void caml_default_remove_last_pin_from_context_hook_r(CAML_R){ */
/*   caml_destroy_context_r(ctx); */
/* } */
/* void (*caml_remove_last_pin_from_context_hook)(CAML_R) = caml_default_remove_last_pin_from_context_hook_r; */



/* CAMLprim int caml_multi_context_implemented(value unit){ */
/* #if HAS_MULTI_CONTEXT */
/*   return Bool_val(1); */
/* #else */
/*   return Bool_val(0); */
/* #endif /\* #if HAS_MULTI_CONTEXT *\/ */
/* } */

__thread int caml_indentation_level = 0; // FIXME: remove this crap after debugging !!!!!!!!!!!!!!!!

// FIXME: remove this kludge

int TRIVIAL_caml_systhreads_get_thread_no_r(CAML_R){
  return 0;
}
int caml_systhreads_get_thread_no_r (CAML_R) __attribute__ ((weak, alias ("TRIVIAL_caml_systhreads_get_thread_no_r")));

int caml_debugging = 0; // !!!!!!!!!!!!!!!!!!!!!!!
