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

/* Start-up code */

#define CAML_CODE_FRAGMENT_TABLE
#define CAML_CONTEXT_STARTUP
#define CAML_CONTEXT_PARSING
#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_FAILROOTS
#define CAML_CONTEXT_DEBUGGER

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h> // FIXME: remove if not needed in the end --Luca Saiu REENTRANTRUNTIME
#include "callback.h"
#include "backtrace.h"
#include "custom.h"
#include "debugger.h"
#include "fail.h"
#include "freelist.h"
#include "gc.h"
#include "gc_ctrl.h"
#include "intext.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "osdeps.h"
#include "printexc.h"
#include "stack.h"
#include "sys.h"
#ifdef HAS_UI
#include "ui.h"
#endif

/* Initialize the atom table and the static data and code area limits. */

struct segment { char * begin; char * end; };

static void init_atoms_r(CAML_R)
{
  extern struct segment caml_data_segments[], caml_code_segments[];
  int i;
  struct code_fragment * cf;

  for (i = 0; i < 256; i++) {
    caml_atom_table[i] = Make_header(0, i, Caml_white);
  }
  if (caml_page_table_add_r(ctx, In_static_data,
                            caml_atom_table, caml_atom_table + 256) != 0)
    caml_fatal_error("Fatal error: not enough memory for the initial page table");

  for (i = 0; caml_data_segments[i].begin != 0; i++) {
    /* PR#5509: we must include the zero word at end of data segment,
       because pointers equal to caml_data_segments[i].end are static data. */
    if (caml_page_table_add_r(ctx, In_static_data,
                              caml_data_segments[i].begin,
                              caml_data_segments[i].end + sizeof(value)) != 0)
      caml_fatal_error("Fatal error: not enough memory for the initial page table");
  }

  caml_code_area_start = caml_code_segments[0].begin;
  caml_code_area_end = caml_code_segments[0].end;
  for (i = 1; caml_code_segments[i].begin != 0; i++) {
    if (caml_code_segments[i].begin < caml_code_area_start)
      caml_code_area_start = caml_code_segments[i].begin;
    if (caml_code_segments[i].end > caml_code_area_end)
      caml_code_area_end = caml_code_segments[i].end;
  }
  /* Register the code in the table of code fragments */
  cf = caml_stat_alloc(sizeof(struct code_fragment));
  cf->code_start = caml_code_area_start;
  cf->code_end = caml_code_area_end;
  cf->digest_computed = 0;
  caml_ext_table_init(&caml_code_fragments_table, 8);
  caml_ext_table_add(&caml_code_fragments_table, cf);
}

/* caml_R: can be shared between threads, as it is only needed on startup */
/* Configuration parameters and flags */

static uintnat percent_free_init = Percent_free_def;
static uintnat max_percent_free_init = Max_percent_free_def;
static uintnat minor_heap_init = Minor_heap_def;
static uintnat heap_chunk_init = Heap_chunk_def;
static uintnat heap_size_init = Init_heap_def;
static uintnat max_stack_init = Max_stack_def;

/* Parse the CAMLRUNPARAM variable */
/* The option letter for each runtime option is the first letter of the
   last word of the ML name of the option (see [stdlib/gc.mli]).
   Except for l (maximum stack size) and h (initial heap size).
*/
/* Note: option l is irrelevant to the native-code runtime. */

/* If you change these functions, see also their copy in byterun/startup.c */

static void scanmult (char *opt, uintnat *var)
{
  char mult = ' ';
  int val;
  sscanf (opt, "=%u%c", &val, &mult);
  sscanf (opt, "=0x%x%c", &val, &mult);
  switch (mult) {
  case 'k':   *var = (uintnat) val * 1024; break;
  case 'M':   *var = (uintnat) val * 1024 * 1024; break;
  case 'G':   *var = (uintnat) val * 1024 * 1024 * 1024; break;
  default:    *var = (uintnat) val; break;
  }
}

static void parse_camlrunparam_r(CAML_R)
{
  char *opt = getenv ("OCAMLRUNPARAM");
  uintnat p;

  if (opt == NULL) opt = getenv ("CAMLRUNPARAM");

  if (opt != NULL){
    while (*opt != '\0'){
      switch (*opt++){
      case 's': scanmult (opt, &minor_heap_init); break;
      case 'i': scanmult (opt, &heap_chunk_init); break;
      case 'h': scanmult (opt, &heap_size_init); break;
      case 'l': scanmult (opt, &max_stack_init); break;
      case 'o': scanmult (opt, &percent_free_init); break;
      case 'O': scanmult (opt, &max_percent_free_init); break;
      case 'v': scanmult (opt, &caml_verb_gc); break;
      case 'b': caml_record_backtrace_r(ctx, Val_true); break;
      case 'p': caml_parser_trace = 1; break;
      case 'a': scanmult (opt, &p); caml_set_allocation_policy_r (ctx, p); break;
      }
    }
  }
}

//extern __thread caml_global_context *caml_context; // in context.c // FIXME: remove this: it's now a thread-local static variable

/* FIXME: refactor: call this from caml_main_rr --Luca Saiu REENTRANTRUNTIME */
caml_global_context* caml_make_empty_context(void)
{
  // FIXME: lock
  /* Make a new context in which to unmarshal back the byte array back
     into a big data structure, copying whatever's needed: */
  //caml_global_context *old_thread_local_context = caml_get_thread_local_context();
  caml_global_context *ctx = caml_initialize_first_global_context();
  ctx->descriptor->kind = caml_global_context_nonmain_local;
  //caml_set_thread_local_context(old_thread_local_context); // undo caml_initialize_first_global_context's trashing of the __thread variable
  // FIXME: unlock

  /* Initialize the abstract machine */
  caml_init_gc_r (ctx, minor_heap_init, heap_size_init, heap_chunk_init,
                  percent_free_init, max_percent_free_init);
  //caml_init_stack_r (ctx, max_stack_init); // Not for native code
  init_atoms_r(ctx);

  /* No need to call caml_init_signals for each context: its
     initialization only needs to be performed once */
  caml_debugger_init_r (ctx); /* force debugger.o stub to be linked */

  /* Make the new context be the thread-local context for this thread: */
  caml_set_thread_local_context(ctx);

  return ctx;
}

extern value caml_start_program_r (CAML_R);
extern void caml_init_ieee_floats (void);
extern void caml_init_signals (void);
extern void caml_debugger_init_r(CAML_R);

caml_global_context* caml_main_rr(char **argv)
{
  char * exe_name;
#ifdef __linux__
  static char proc_self_exe[256];
#endif
  value res;
  char tos;
  caml_context_initialize_global_stuff();
  CAML_R = caml_initialize_first_global_context();
  the_main_context = ctx;

  caml_init_ieee_floats();
  caml_init_custom_operations();
#ifdef DEBUG
  caml_verb_gc = 63;
#endif
  caml_top_of_stack = &tos;
  parse_camlrunparam_r(ctx);
  caml_init_gc_r (ctx, minor_heap_init, heap_size_init, heap_chunk_init,
                percent_free_init, max_percent_free_init);

  /* ctx->caml_global_data is only used for bytecode */
  /* ctx->caml_global_data = Val_int(42); // Unused with native code: set to a valid OCaml value, and forget about it. */
/*   caml_oldify_one_r (ctx, ctx->caml_global_data, &ctx->caml_global_data); */
/*   caml_oldify_mopup_r (ctx); // FIXME: what's this for, exactly?  --Luca Saiu REENTRANTRUNTIME */

  init_atoms_r(ctx);
  caml_init_signals();
  caml_debugger_init_r (ctx); /* force debugger.o stub to be linked */
  exe_name = argv[0];
  if (exe_name == NULL) exe_name = "";
#ifdef __linux__
  if (caml_executable_name(proc_self_exe, sizeof(proc_self_exe)) == 0)
    exe_name = proc_self_exe;
  else
    exe_name = caml_search_exe_in_path(exe_name);
#else
  exe_name = caml_search_exe_in_path(exe_name);
#endif
  caml_sys_init_r(ctx, exe_name, argv);
  if (sigsetjmp(caml_termination_jmpbuf.buf, 0)) {
    if (caml_termination_hook != NULL) caml_termination_hook();
    return ctx;
  }

  // Before my experimental changes: begin --Luca Saiu REENTRANTRUNTIME
  /* res = caml_start_program_r(ctx); */
  /* if (Is_exception_result(res)) */
  /*   caml_fatal_uncaught_exception_r(ctx, Extract_exception(res)); */

  /* fprintf(stderr, "HHH OK2\n"); */

  /* return ctx; */
  // Before my experimental changes: end --Luca Saiu REENTRANTRUNTIME

  //// Very experimental: begin --Luca Saiu REENTRANTRUNTIME
  
  //fprintf(stderr, "caml_main_rr: setjmp'ing [%p]\n", *((void**)(ctx->where_to_longjmp)));
  if(setjmp(ctx->where_to_longjmp)){
    fprintf(stderr, "caml_main_rr: back from a longjmp [%p]\n", *((void**)(ctx->where_to_longjmp)));
    //fprintf(stderr, "In the parent context caml_bottom_of_stack is %p\n", caml_bottom_of_stack);    ////
    //caml_init_gc_r (ctx->after_longjmp_context, minor_heap_init, heap_size_init, heap_chunk_init, percent_free_init, max_percent_free_init);
    // Very experimental.  Begin.  What is happening here?
    // caml_top_of_stack = &tos;
    //caml_init_gc_r (ctx, minor_heap_init, heap_size_init, heap_chunk_init, percent_free_init, max_percent_free_init);
    // Very experimental.  End.  What is happening here?
    ctx->after_longjmp_function(ctx->after_longjmp_context,
                                ctx->after_longjmp_serialized_blob);
    return NULL; /* this should be unreachable */
  }
  else{
    //fprintf(stderr, "caml_main_rr: right after the setjmp call [%p]\n", *((void**)(ctx->where_to_longjmp)));
    res = caml_start_program_r(ctx);
    if (Is_exception_result(res))
      caml_fatal_uncaught_exception_r(ctx, Extract_exception(res));
    //fprintf(stderr, "caml_main_rr: exiting normally\n");
    return ctx;
  }
  //// Very experimental: end --Luca Saiu REENTRANTRUNTIME
}

void caml_startup(char **argv)
{
  caml_main_rr(argv);
}
