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
#include <string.h>

#define __USE_UNIX98
#include <pthread.h>

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

__thread caml_global_context *caml_context;

caml_global_context *caml_get_global_context(void)
{
  /* fprintf(stderr, "get caml_context %x\n", caml_context); */
  return caml_context;
}

void caml_set_global_context(caml_global_context *new_caml_context)
{
  caml_context = new_caml_context;
}

extern void caml_enter_blocking_section_default(void);
extern void caml_leave_blocking_section_default(void);
extern int caml_try_leave_blocking_section_default(void);


#ifdef NATIVE_CODE
extern char caml_globals_map[];
#endif

/* The global lock: */
static pthread_mutex_t caml_global_mutex;

caml_global_context *caml_initialize_first_global_context(void)
{
  /* Maybe we should use partial contexts for specific tasks, that
will probably not be used by all threads.  We should check the size of
each part of the context, to allocate only what is probably required
by all threads, and then allocate other sub-contexts on demand. */

  caml_global_context* ctx = (caml_global_context*)malloc( sizeof(caml_global_context) );
  memset(ctx, 0, sizeof(caml_global_context));

#ifdef NATIVE_CODE
  /* TODO : only for first context ! We should implement a way, so
that calling several times caml_main will actually start several
runtimes, with different contexts. A thread would then be able to
schedule several ocaml runtimes ! Protect this with a protected
section.  */
  //  ctx->caml_globals_map = caml_globals_map; // FIXME: this is Fabrice's version; I really have no reason to change it, except to see the effect --Luca Saiu REENTRANTRUNTIME
  ctx->caml_globals_map = NULL; // FIXME: horrible, horrible test.  I'm intentionally breaking Fabrice's code to see what breaks [nothing, apparently].  --Luca Saiu REENTRANTRUNTIME
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

  /* from majoc_gc.c */
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
  ctx->caml_scan_roots_hook = NULL;
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
  ctx->caml_scan_roots_hook = NULL;
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

  ctx->enter_blocking_section_hook = &caml_enter_blocking_section_default;
  ctx->leave_blocking_section_hook = &caml_leave_blocking_section_default;
  ctx->try_leave_blocking_section_hook = &caml_try_leave_blocking_section_default;

  ctx->caml_force_major_slice = 0;
  ctx->caml_signal_handlers = 0;

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

  /* from callback.c */
  ctx->caml_callback_depth = 0;
  ctx->callback_code_threaded = 0;
  ctx->named_value_table[0] = NULL;

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

  caml_context = ctx;
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

  /* Global context-local OCaml variables */
  ctx->caml_globals.allocated_size = INITIAL_CAML_GLOBALS_ALLOCATED_SIZE;
  ctx->caml_globals.used_size = 0;
  ctx->caml_globals.array = caml_stat_alloc(ctx->caml_globals.allocated_size);

  /* Global context-local C variables */
  ctx->c_globals.allocated_size = INITIAL_C_GLOBALS_ALLOCATED_SIZE;
  ctx->c_globals.used_size = 0;
  ctx->c_globals.array = caml_stat_alloc(ctx->c_globals.allocated_size);
  
  /* Make a local descriptor for this context: */
  ctx->descriptor = caml_stat_alloc(sizeof(struct caml_global_context_descriptor));
  ctx->descriptor->kind = caml_global_context_main;
  ctx->descriptor->content.local_context.context = ctx;

  /* Create the global lock: */
  pthread_mutexattr_t attributes;
  pthread_mutexattr_init(&attributes);
  pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init(&caml_global_mutex, &attributes);
  pthread_mutexattr_destroy(&attributes);

  return ctx;
}

/* Return an index, in words */
int caml_allocate_caml_globals_r(CAML_R, size_t added_caml_global_no){
  size_t new_used_size =
    caml_allocate_from_extensible_buffer(&ctx->caml_globals, added_caml_global_no * sizeof(value), /*as the least-significant byte, this yields a non-pointer*/1);
  return new_used_size / sizeof(value);
}

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
  int i, caml_global_no = ctx->caml_globals.used_size / sizeof(value);
  //if(caml_global_no != 0) printf("Context %p: scanning the %i Caml globals\n", ctx, caml_global_no);
  value *caml_globals = (value*)(ctx->caml_globals.array);
  for(i = 0; i < caml_global_no; i ++){
    value *root_pointer = caml_globals + i;
    f(ctx, *root_pointer, root_pointer);
  }
  //printf("Scanning Caml globals: end\n");
}

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
}

void caml_leave_lock_section_default(void)
{
}

/* I'm leaving these as globals, shared bu all contexts.  --Luca Saiu REENTRANTRUNTIME*/
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

/* the first other context (at pos 0) is always NULL, so that we are sure the first
   line of caml_get_library_context_r is ok to execute. */
static int nbr_other_contexts = 0;

library_context *caml_get_library_context_r(
					    CAML_R,
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

// FIXME: caml_realloc_global_r (meta.c) is a dummy stub on the native compiler.  --Luca Saiu REENTRANTRUNTIME

/* void caml_resize_global_array_r(CAML_R, size_t requested_global_no){ */
/*   size_t old_global_no = Wosize_val(ctx->caml_global_data); */
/*   size_t new_global_no = old_global_no; */
/*   value new_array; */
/*   int i; */

/*   fprintf(stderr, "**           old size: %i\n", (int)old_global_no); */
/*   fprintf(stderr, "** requested new size: %i\n", (int)requested_global_no); */
/*   /\* Don't ever shrink: *\/ */
/*   if(requested_global_no <= old_global_no) */
/*     return; */

/*   /\* Find a size large enough to accommodate the given global no, but potentially larger. */
/*      We want to minimize the number of resizing: *\/ */
/*   while(new_global_no < requested_global_no) */
/*     new_global_no *= 2; */

/*   /\* Copy the already-used elements, and simply zero the rest: *\/ */
/*   new_array = caml_alloc_shr_r(ctx, new_global_no, 0); */
/*   fprintf(stderr, "** caml_resize_global_array_r: we have now reserved space for %i globals\n", (int)new_global_no); */
/*   for (i = 0; i < old_global_no; i ++) */
/*     caml_initialize_r(ctx, &Field(new_array, i), Field(ctx->caml_global_data, i)); */
/*   for (; i < new_global_no; i ++) */
/*     caml_initialize_r(ctx, &Field(new_array, i), Val_long(0)); */

/*   /\* Make the new global array the "official" one for this context, */
/*      and oldify it so we don't waste time scanning it too many times. */
/*      The old global array will be garbage-collected. *\/ */
/*   ctx->caml_global_data = new_array; */
/*   caml_oldify_one_r (ctx, ctx->caml_global_data, &ctx->caml_global_data); */
/*   caml_oldify_mopup_r (ctx); */
/* } */

/* The index of the first word in caml_globals which is not used yet.
   This variable is shared by all contexts, and accessed in mutual
   exclusion. */
static long first_unused_word_offset = 0; // the first word is unused

void caml_register_module_r(CAML_R, size_t size_in_bytes, long *offset_pointer){
  /* Compute the size in words, which is to say how many globals are there: */
  int size_in_words = size_in_bytes / sizeof(void*);
  Assert(size_in_words * sizeof(void*) == size_in_bytes); /* there's a whole number of globals */
  /* fprintf(stderr, "caml_register_module_r: BEGIN [%lu bytes at %p]\n", */
  /*        (unsigned long)size_in_bytes, */
  /*        offset_pointer); */

  /* If this is the first time we register this module, make space for its globals in
     ctx->caml_globals.  If the module was already registered, do nothing. */
  caml_enter_lock_section_r(ctx);
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
  caml_leave_lock_section_r(ctx);
  /* fprintf(stderr, "The offset (in bytes) we just wrote at %p is %li\n", offset_pointer, *offset_pointer); */
  /* fprintf(stderr, "The context is at %p\n", (void*)ctx); */
  /* fprintf(stderr, "Globals are at %p\n", (void*)ctx->caml_globals.array); */
  /* fprintf(stderr, "caml_register_module_r: registered %p.  END (still alive)\n\n", offset_pointer); */
}

void caml_after_module_initialization_r(CAML_R, size_t size_in_bytes, long *offset_pointer){
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
  return Val_bool(caml_global_context_descriptor_of_value(descriptor)->kind
                  == caml_global_context_main);
}

CAMLprim value caml_context_is_remote_r(CAML_R, value descriptor)
{
  return Val_bool(caml_global_context_descriptor_of_value(descriptor)->kind
                  == caml_global_context_remote);
}

/* /\* A function with an interface easier to call from OCaml: *\/ */
/* CAM__Lprim value caml_context_clone_and_return_value_r(CAML_R, value unit){ */
/*   return Val_unit; // FIXME: remove this function */
/* } */

void caml_acquire_global_lock_r(CAML_R){
  /* FIXME: is this needed?  I wanna play it safe --Luca Saiu REENTRANTRUNTIME */
  int result __attribute__((unused));
  caml_enter_lock_section_r(ctx);
  result = pthread_mutex_lock(&caml_global_mutex);
  Assert(result == 0);
}
void caml_release_global_lock_r(CAML_R){
  int result __attribute__((unused)) = pthread_mutex_unlock(&caml_global_mutex);
  Assert(result == 0);
  /* FIXME: is this needed?  I wanna play it safe --Luca Saiu REENTRANTRUNTIME */
  caml_leave_lock_section_r(ctx);
}
