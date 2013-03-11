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
/*                       OCamlPro   (2011)                             */
/*                                                                     */
/*                  Copyright 2011 -- OCamlPro SAS                     */
/*                                                                     */
/***********************************************************************/

#ifndef CAML_CONTEXT_H
#define CAML_CONTEXT_H

/* If these includes are missing, the offsets of fields might differ ! */
#include <signal.h>
#include <setjmp.h> // FIXME: remove if not needed in the end --Luca Saiu REENTRANTRUNTIME

#define __USE_UNIX98
#include <pthread.h>
#include <semaphore.h>

#include "config.h"
#include "mlvalues.h"
#include "misc.h"
#include "extensible_buffer.h"

/* An initialization function to be called at startup, once and only once: */
void caml_context_initialize_global_stuff(void);


/* The sentinel can be located anywhere in memory, but it must not be
   adjacent to any heap object. */
typedef struct fl_sentinel {
  value filler1; /* Make sure the sentinel is never adjacent to any block. */
  header_t h;
  value first_bp;
  value filler2; /* Make sure the sentinel is never adjacent to any block. */
} fl_sentinel;
/* initialized with = {0, Make_header (0, 0, Caml_blue), 0, 0}; */


struct caml_minor_ref_table {
  value **base;
  value **end;
  value **threshold;
  value **ptr;
  value **limit;
  asize_t size;
  asize_t reserve;
};

/* Trail mechanism to undo forwarding pointers put inside objects */

struct trail_entry {
  value obj;    /* address of object + initial color in low 2 bits */
  value field0; /* initial contents of field 0 */
};

/* also defined in intext.h */
#define ENTRIES_PER_TRAIL_BLOCK  1025
#define SIZE_EXTERN_OUTPUT_BLOCK 8100

struct trail_block {
  struct trail_block * previous;
  struct trail_entry entries[ENTRIES_PER_TRAIL_BLOCK];
};

#ifdef NATIVE_CODE
/* from asmrun/roots.c */
/* Linked-list */

typedef struct caml_link {
  void *data;
  struct caml_link *next;
} caml_link;

#endif

struct output_block {
  struct output_block * next;
  char * end;
  char data[SIZE_EXTERN_OUTPUT_BLOCK];
};

struct final {
  value fun;
  value val;
  int offset;
};

struct to_do {
  struct to_do *next;
  int size;
  struct final item[1];  /* variable size */
};

struct named_value {
  value val;
  struct named_value * next;
  char name[1];
};

#ifdef ARCH_SIXTYFOUR

/* 64-bit implementation:
   The page table is represented sparsely as a hash table
   with linear probing */

struct page_table {
  mlsize_t size;                /* size == 1 << (wordsize - shift) */
  int shift;
  mlsize_t mask;                /* mask == size - 1 */
  mlsize_t occupancy;
  uintnat * entries;            /* [size]  */
};

#endif

/* from fail.h */

#include <setjmp.h>

#ifdef POSIX_SIGNALS
struct longjmp_buffer {
  sigjmp_buf buf;
};
#else
struct longjmp_buffer {
  jmp_buf buf;
};
#define sigsetjmp(buf,save) setjmp(buf)
#define siglongjmp(buf,val) longjmp(buf,val)
#endif


typedef struct caml_global_context /* volatile */ caml_global_context;

typedef void (*scanning_action) (caml_global_context *, value, value *);

/* globroots.c */
/* The sets of global memory roots are represented as skip lists
   (see William Pugh, "Skip lists: a probabilistic alternative to
   balanced binary trees", Comm. ACM 33(6), 1990). */

struct global_root {
  value * root;                    /* the address of the root */
  struct global_root * forward[1]; /* variable-length array */
};

#define NUM_LEVELS 17

struct global_root_list {
  value * root;                 /* dummy value for layout compatibility */
  struct global_root * forward[NUM_LEVELS]; /* forward chaining */
  int level;                    /* max used level */
};

CAMLextern void (*caml_scan_roots_hook) (scanning_action);


struct compare_item { value * v1, * v2; mlsize_t count; };


typedef struct library_context library_context;

/* From extern.c: */
struct extern_item { value * v; mlsize_t count; }; /* Stack holding pending values to marshal */
#define EXTERN_STACK_INIT_SIZE 256
#define EXTERN_STACK_MAX_SIZE (1024*1024*100)
enum { NO_SHARING = 1, CLOSURES = 2, CROSS_CONTEXT = 4 };

struct caml_mailbox{
  /* By convention a mailbox "belongs" to the context which created it: */
  struct caml_global_context_descriptor *descriptor;

#define CAML_INITIAL_ALLOCATED_MESSAGE_NO 10
  //#define CAML_QUEUE_SLOT_NO 8
  /* The message queue, and its synchronization structures. */
  /* FIXME: implement an efficient queue (in a separate file).  This
     is currently a left-aligned array (with the unused elements on
     the right) where elements are enqueued on the right, and dequeued
     on the left.  Dequeuing is expensive, since elements have to be
     shifted by one position. */
  struct caml_message{
    char *message_blob; // malloc'd
  } //message_queue[CAML_QUEUE_SLOT_NO];
    *message_queue;
  int allocated_message_no;
  sem_t message_no_semaphore;   /* a semaphore counting messages */
  int message_no; /* same value as the semaphore; needed for synchronization reasons */
  //sem_t free_slot_no_semaphore;   /* a semaphore counting free slots */
  pthread_mutex_t mutex;
}; // struct


/* From st_posix.h: */
/* The master lock.  This is a mutex that is held most of the time,
   so we implement it in a slightly consoluted way to avoid
   all risks of busy-waiting.  Also, we count the number of waiting
   threads. */
typedef struct {
  pthread_mutex_t lock;         /* to protect contents  */
  int busy;                     /* 0 = free, 1 = taken */
  volatile int waiters;         /* number of threads waiting on master lock */
  pthread_cond_t is_free;       /* signaled when free */
} st_masterlock;


// ??????
typedef struct caml_thread_struct * caml_thread_t; /* from st_posix.h */
typedef pthread_t st_thread_id; /* from st_posix.h */
// ??????

/* The field ordering should not be changed without also updating the
   macro definitions at the beginning of asmrun/ARCHITECTURE.s. */
struct caml_global_context {

  /* 0 */ char *caml_young_limit;        /* minor_gc.c */
  /* 1 */ char *caml_young_ptr;          /* minor_gc.c */
#ifdef NATIVE_CODE
  /* 2 */ uintnat caml_last_return_address;
  /* 3 */ char * caml_bottom_of_stack;
  /* 4 */ value * caml_gc_regs;
  /* 5 */ char * caml_exception_pointer;
  /* 6 */ int caml_backtrace_active;
  int padding1; /* FIXME: is there some good reason for caml_backtrace_active to
                   be int rather than long?  --Luca Saiu REENTRANTRUNTIME */
  /* Context-local "global" OCaml variables: */
#define INITIAL_CAML_GLOBALS_ALLOCATED_SIZE 8 /* in bytes */
  /* 7, 8, 9 */struct caml_extensible_buffer caml_globals; /* = {dynamic, INITIAL_CAML_GLOBALS_ALLOCATED_SIZE, 0} */
#endif

#ifndef NATIVE_CODE
  // FIXME: should I rename this?  It's not clear from tha name that this is bytecode-only
  value caml_global_data; // Moved by Luca Saiu REENTRANTRUNTIME: FIXME: I can put it back where it was
#endif /* #ifndef NATIVE_CODE */


#ifdef NATIVE_CODE
  char* caml_globals_map;
#endif

  /* from stacks.c */
#ifdef NATIVE_CODE

#else

  uintnat (*caml_stack_usage_hook)();
  value * caml_stack_low;
  value * caml_stack_high;
  value * caml_stack_threshold;
  value * caml_extern_sp;
  value * caml_trapsp;
  value * caml_trap_barrier;
  uintnat caml_max_stack_size;            /* also used in gc_ctrl.c */
#endif

  /* from startup.c */
  header_t caml_atom_table[256];
#ifdef NATIVE_CODE
// FIXME: I'm quite sure that moving these here was wrong --Luca Saiu REENTRANTRUNTIME
  char * caml_code_area_start;
  char * caml_code_area_end;

/* These are termination hooks used by the systhreads library */
  struct longjmp_buffer caml_termination_jmpbuf;
  void (*caml_termination_hook)(void);

#endif

  /* from majoc_gc.c */
  uintnat caml_percent_free;
  uintnat caml_major_heap_increment;
  char *caml_heap_start;
  char *caml_gc_sweep_hp;
  int caml_gc_phase;        /* always Phase_mark, Phase_sweep, or Phase_idle */
  value *gray_vals;
  value *gray_vals_cur, *gray_vals_end;
  asize_t gray_vals_size;
  int heap_is_pure;   /* The heap is pure if the only gray objects
			 below [markhp] are also in [gray_vals]. */
  uintnat caml_allocated_words;
  uintnat caml_dependent_size;
  uintnat caml_dependent_allocated;
  double caml_extra_heap_resources;
  uintnat caml_fl_size_at_phase_change;
  char *markhp;
  char *mark_chunk; /* instead of chunk */
  char *mark_limit; /* instead of limit */
  int caml_gc_subphase;     /* Subphase_{main,weak1,weak2,final} */
  value *weak_prev;

  /* from freelist.c */
  fl_sentinel sentinel;
  char *fl_prev; /* = Fl_head; */  /* Current allocation pointer. */
  char *fl_last; /* = NULL; */    /* Last block in the list.  Only valid
                                 just after [caml_fl_allocate] returns NULL. */
  char *caml_fl_merge; /* = Fl_head; */   /* Current insertion pointer.  Managed
                                    jointly with [sweep_slice]. */
  asize_t caml_fl_cur_size; /* = 0; */    /* Number of words in the free list,
                                    including headers but not fragments. */
#define FLP_MAX 1000
  char *flp [FLP_MAX];
  int flp_size; /* = 0; */
  char *beyond; /* = NULL; */
#define Policy_next_fit 0
#define Policy_first_fit 1
  uintnat caml_allocation_policy; /* = Policy_next_fit; */
#define policy caml_allocation_policy


  /* from minor_gc.c */
  asize_t caml_minor_heap_size;
  void *caml_young_base;        /*  = NULL;  */
  char *caml_young_start;       /* = NULL */
  char *caml_young_end;         /*  = NULL; */
  struct caml_minor_ref_table
  caml_ref_table; /* = { NULL, NULL, NULL, NULL, NULL, 0, 0}, */
  struct caml_minor_ref_table caml_weak_ref_table;
  /* = { NULL, NULL, NULL, NULL, NULL, 0, 0}; */
  int caml_in_minor_collection;  /*  = 0; */
  value oldify_todo_list;

  /* from memory.h */
#ifdef ARCH_SIXTYFOUR
  struct page_table caml_page_table;
#else
  unsigned char * caml_page_table[Pagetable1_size];
  unsigned char caml_page_table_empty[Pagetable2_size]; /* = { 0, }; */
#endif

  /* from roots.c */
#ifdef NATIVE_CODE

  /* Roots registered from C functions */
  struct caml__roots_block *caml_local_roots;
  /* Communication with [caml_start_program] and [caml_call_gc]. */

  /* FIXME: This is the version by Fabrice, which looks out-of-date
     with respect to his changes in asmrun/roots.c.  Keeping it as it is
     in this comment, for the time being  --Luca Saiu REENTRANTRUNTIME */
  /* char * caml_top_of_stack; */
  /* intnat caml_globals_inited; */
  /* intnat caml_globals_scanned; */
  /* caml_link * caml_dyn_globals; */
  /* uintnat (*caml_stack_usage_hook)(void); */

  char * caml_top_of_stack;
  //char * caml_bottom_of_stack /* = NULL */; /* no stack initially */
  //uintnat caml_last_return_address /* = 1 */; /* not in OCaml code initially */
  //value * caml_gc_regs;
  intnat caml_globals_inited /* = 0 */;
  intnat caml_globals_scanned /* = 0 */;
  caml_link * caml_dyn_globals /* = NULL */;
  uintnat (*caml_stack_usage_hook)(void);
  
#else

  struct caml__roots_block *caml_local_roots; /*  = NULL;  */

#endif

  /* from globroots.c */
  uint32 random_seed; /*  = 0; */
  /* The three global root lists */
  struct global_root_list caml_global_roots; /*  = { NULL, { NULL, }, 0 }; */
  /* mutable roots, don't know whether old or young */
  struct global_root_list caml_global_roots_young; /* = { NULL, { NULL, }, 0 }; */
  /* generational roots pointing to minor or major heap */
  struct global_root_list caml_global_roots_old; /* = { NULL, { NULL, }, 0 }; */
  /* generational roots pointing to major heap */

  /* from fail.c */
#ifdef NATIVE_CODE
  int array_bound_error_bucket_inited;
#else
  struct longjmp_buffer * caml_external_raise; /*  = NULL;  */
  value caml_exn_bucket;
  struct {
    header_t hdr;
    value exn;
  } out_of_memory_bucket /* = { 0, 0 } */;
#endif

  /* from signals_byt.c */
  int volatile caml_something_to_do; /*  = 0; */
  void (* volatile caml_async_action_hook)(void); /* = NULL; */

  /* from signals.c */
  /* The set of pending signals (received but not yet processed) */
  intnat caml_signals_are_pending; /* = 0; */
#ifndef NSIG
#define NSIG 64 // FIXME: this same conditional #define'ition is repeated several times in different places.
#endif /* #ifndef NSIG */
  intnat caml_pending_signals[NSIG];
  intnat caml_async_signal_mode; /* = 0; */
  void (*caml_enter_blocking_section_hook)(void);
  void (*caml_leave_blocking_section_hook)(void);
  int (*caml_try_leave_blocking_section_hook)(void);
  int caml_force_major_slice; /* = 0; */
  value caml_signal_handlers; /* = 0; */

  /* from backtrace.c */
#ifdef NATIVE_CODE

  int caml_backtrace_pos;
  code_t * caml_backtrace_buffer;
  value caml_backtrace_last_exn;

#else
  int caml_backtrace_active;  /*  = 0;   */
  int caml_backtrace_pos;  /*  = 0; */
  code_t * caml_backtrace_buffer; /* = NULL; */
  value caml_backtrace_last_exn; /* = Val_unit; */
  char * caml_cds_file; /*  = NULL; */
#endif

  /* from compare.c */
/* Structural comparison on trees. */

#define COMPARE_STACK_INIT_SIZE 256
#define COMPARE_STACK_MAX_SIZE (1024*1024)

  struct compare_item compare_stack_init[COMPARE_STACK_INIT_SIZE];
  struct compare_item * compare_stack; /* = compare_stack_init; */
  struct compare_item * compare_stack_limit; /* = compare_stack_init
						+ COMPARE_STACK_INIT_SIZE; */
  int caml_compare_unordered;

  /* from sys.c */
  char * caml_exe_name;
  char ** caml_main_argv;

  /* from extern.c */
  uintnat obj_counter;  /* Number of objects emitted so far */
  uintnat size_32;  /* Size in words of 32-bit block for struct. */
  uintnat size_64;  /* Size in words of 64-bit block for struct. */
  int extern_ignore_sharing; /* Flag to ignore sharing */
  int extern_closures;       /* Flag to allow externing code pointers */
  int extern_cross_context;  /* Flag to marshal at context-split time */
  struct trail_block extern_trail_first;
  struct trail_block * extern_trail_block;
  struct trail_entry * extern_trail_cur;
  struct trail_entry * extern_trail_limit;
  char * extern_userprovided_output;
  char * extern_ptr;
  char * extern_limit;
  struct output_block * extern_output_first;
  struct output_block * extern_output_block;
  struct extern_item extern_stack_init[EXTERN_STACK_INIT_SIZE];
  struct extern_item * extern_stack;/* = extern_stack_init;*/
  struct extern_item * extern_stack_limit; /* = extern_stack_init + EXTERN_STACK_INIT_SIZE; */
  int extern_flags[3];

  /* from intext.h [I suppose the global was decleared in a header by
     mistake; but I might also be missing some subtle reason --Luca Saiu] */
  struct ext_table caml_code_fragments_table;
  
  /* from intern.c */
  unsigned char * intern_src;
  /* Reading pointer in block holding input data. */
  unsigned char * intern_input;
  /* Pointer to beginning of block holding input data.
     Meaningful only if intern_input_malloced = 1. */
  int intern_input_malloced;
  /* 1 if intern_input was allocated by caml_stat_alloc()
     and needs caml_stat_free() on error, 0 otherwise. */
  header_t * intern_dest;
  /* Writing pointer in destination block */
  char * intern_extra_block;
  /* If non-NULL, point to new heap chunk allocated with caml_alloc_for_heap. */
  /*  asize_t obj_counter; TODO: duplicated from extern.c, since both cannot be in use at the same time */
  /* Count how many objects seen so far */
  value * intern_obj_table;
  /* The pointers to objects already seen */
  unsigned int intern_color;
  /* Color to assign to newly created headers */
  header_t intern_header;
  /* Original header of the destination block.
     Meaningful only if intern_extra_block is NULL. */
  value intern_block;
  /* Point to the heap block allocated as destination block.
     Meaningful only if intern_extra_block is NULL. */
  value * camlinternaloo_last_id /* = NULL */;


  /* from gc_ctrl.c */
  double caml_stat_minor_words; /* = 0.0, */
  double caml_stat_promoted_words; /* = 0.0, */
  double caml_stat_major_words; /* = 0.0; */

  intnat caml_stat_minor_collections; /* = 0, */
  intnat caml_stat_major_collections; /* = 0, */
  intnat caml_stat_heap_size; /*  = 0, */              /* bytes */
  intnat caml_stat_top_heap_size; /* = 0, */          /* bytes */
  intnat caml_stat_compactions;  /* = 0, */
  intnat caml_stat_heap_chunks;  /* = 0; */
  uintnat caml_percent_max;  /* used in gc_ctrl.c and memory.c */

  /* from compact.c */
  char *compact_fl;

  /* from callback.c */
  int caml_callback_depth; /* = 0; */
  int callback_code_threaded; /* = 0; */
/* Naming of Caml values */
#define Named_value_size 13
  struct named_value * named_value_table[Named_value_size]; /* = { NULL, }; */

  /* fromm debugger.c */
  int caml_debugger_in_use; /*  = 0; */
  uintnat caml_event_count;
  int caml_debugger_fork_mode; /* = 1; */ /* parent by default */
  value marshal_flags; /*= Val_emptylist;*/

  /* from weak.c */
  value caml_weak_list_head; /* = 0; */

  /* from finalise.c */
  struct final *final_table; /* = NULL; */
  uintnat final_old; /* = 0, */
  uintnat final_young; /* = 0, */
  uintnat final_size;   /*  = 0; */
/* [0..old) : finalisable set
   [old..young) : recent set
   [young..size) : free space
*/
  struct to_do *to_do_hd; /*  = NULL; */
  struct to_do *to_do_tl; /* = NULL; */
  int running_finalisation_function; /*  = 0; */



  /* from dynlink.c */
/* The table of primitives */
  struct ext_table caml_prim_table;

/* The names of primitives (for instrtrace.c) */
  struct ext_table caml_prim_name_table;

/* The table of shared libraries currently opened */
  struct ext_table shared_libs;

/* The search path for shared libraries */
  struct ext_table caml_shared_libs_path;

  /* from parsing.c */
  int caml_parser_trace; /* = 0; */

#define MAX_OTHER_CONTEXTS 64
  library_context* other_contexts[MAX_OTHER_CONTEXTS+1];

  /* From fix_code.c */
  code_t caml_start_code;
  asize_t caml_code_size;
  unsigned char * caml_saved_code;
#ifdef THREADED_CODE
  char ** caml_instr_table;
  char * caml_instr_base;
#endif

  /* From st_stubs.c */
  /* The "head" of the circular list of thread descriptors */
  caml_thread_t all_threads /* = NULL */;

  /* The descriptor for the currently executing thread */
  caml_thread_t curr_thread /* = NULL */;

  /* The master lock protecting the OCaml runtime system */
  st_masterlock caml_master_lock;

  /* Whether the ``tick'' thread is already running */
  int caml_tick_thread_running /* = 0 */;

  /* The thread identifier of the ``tick'' thread */
  st_thread_id caml_tick_thread_id;

  /* Context-local "global" C variables: */
#define INITIAL_C_GLOBALS_ALLOCATED_SIZE 16
  struct caml_extensible_buffer c_globals; /* = {INITIAL_C_GLOBALS_ALLOCATED_SIZE, 0, dynamic} */

  /* Our (local) descriptor: */
  struct caml_global_context_descriptor *descriptor;

  /* Where to longjmp when executing a split context thunk: */
  jmp_buf where_to_longjmp;
  /* Procedure to execute after longjmp: */
  void (*after_longjmp_function)(struct caml_global_context*, char*);
  /* Procedure parameters: */
  struct caml_global_context *after_longjmp_context;
  char *after_longjmp_serialized_blob;

  /* The (POSIX) thread associated to this context: */
  pthread_t thread;

  /* Protect context fields from concurrent accesses: */
  pthread_mutex_t mutex;

  /* The "kludigsh self-pointer"; this is handy for compatibility
     macros translating X to ctx->X.  This field points to the
     structure itself, so that the expressions ctx->X and
     ctx->ctx->X refer the same value -- also as l-values. */
  struct caml_global_context *ctx;
}; /* struct caml_global_context */

/* Context descriptors may be either local or remote: */
enum caml_global_context_descriptor_kind{
  caml_global_context_main,
  caml_global_context_nonmain_local,
  caml_global_context_remote, // FIXME: remove
  caml_global_context_dead
}; /* enum caml_global_context_kind */

/* A local context descriptor trivially refers a context.  This is
   used for the main context and for non-main local contexts. */
struct caml_local_context_descriptor{
  struct caml_global_context *context;
}; /* struct caml_local_context */

/* A stub identifying a context in an address space different from ours. */
struct caml_remote_context_descriptor{
  /* Address space reference: */
  // FIXME: remote address space reference, for example IP address, port and pid
  // FIXME: unique arbitrary identifier, to recognize the specific context within its remote address space
}; /* struct caml_remote_context */

/* Each context has exactly one local context descriptor, created at
   the same time as its context. */
struct caml_global_context_descriptor{
  enum caml_global_context_descriptor_kind kind;
  union{
    struct caml_remote_context_descriptor remote_context;
    struct caml_local_context_descriptor local_context;
  } content; /* FIXME: Can I use an anonymous union, instead of this, pretty-please?  --Luca Saiu REENTRANTRUNTIME */
}; /* struct caml_global_context_descriptor */

/* FIXME: shall I use shorter names?  It's crucial that these are context
   descriptors, not contexts. --Luca Saiu REENTRANTRUNTIME */
value caml_value_of_context_descriptor(struct caml_global_context_descriptor *c);
struct caml_global_context_descriptor* caml_global_context_descriptor_of_value(value v);
value caml_value_of_mailbox(struct caml_mailbox *m);
struct caml_mailbox* caml_mailbox_of_value(value v);

#define CAML_R caml_global_context * /* volatile */ ctx
#define INIT_CAML_R CAML_R = caml_get_thread_local_context()

extern caml_global_context *caml_initialize_first_global_context(void);
extern caml_global_context *caml_make_empty_context(void); /* defined in startup.c */
extern void caml_destroy_context(caml_global_context *c);

/* Access a thread-local context pointer */
extern caml_global_context *caml_get_thread_local_context(void);
extern void caml_set_thread_local_context(caml_global_context *new_global_context);

extern void (*caml_enter_lock_section_hook)(void);
extern void (*caml_leave_lock_section_hook)(void);
extern void caml_enter_lock_section_r(CAML_R);
extern void caml_leave_lock_section_r(CAML_R);
extern library_context *caml_get_library_context_r(
					    CAML_R,
					    int* library_context_pos,
					    int sizeof_library_context,
					    void (*library_context_init_hook)(library_context*));

#ifdef CAML_CONTEXT_MAJOR_GC
#define caml_percent_free ctx->caml_percent_free
#define caml_major_heap_increment ctx->caml_major_heap_increment
#define caml_heap_start ctx->caml_heap_start
#define caml_gc_sweep_hp ctx->caml_gc_sweep_hp
#define caml_gc_phase ctx->caml_gc_phase
#define gray_vals ctx->gray_vals
#define gray_vals_cur ctx->gray_vals_cur
#define gray_vals_end ctx->gray_vals_end
#define gray_vals_size ctx->gray_vals_size
#define heap_is_pure ctx->heap_is_pure
#define caml_allocated_words ctx->caml_allocated_words
#define caml_dependent_size ctx->caml_dependent_size
#define caml_dependent_allocated ctx->caml_dependent_allocated
#define caml_extra_heap_resources ctx->caml_extra_heap_resources
#define caml_fl_size_at_phase_change ctx->caml_fl_size_at_phase_change
#define markhp ctx->markhp
#define mark_chunk ctx->mark_chunk
#define mark_limit ctx->mark_limit
#define caml_gc_subphase ctx->caml_gc_subphase
#define weak_prev ctx->weak_prev
#endif


#ifdef CAML_CONTEXT_FREELIST
#define sentinel ctx->sentinel
#define fl_prev  ctx->fl_prev
#define fl_last  ctx->fl_last
#define caml_fl_merge     ctx->caml_fl_merge
#define caml_fl_cur_size  ctx->caml_fl_cur_size
#define flp       ctx->flp
#define flp_size  ctx->flp_size
#define beyond    ctx->beyond
#define caml_allocation_policy   ctx->caml_allocation_policy
#endif

#ifdef CAML_CONTEXT_MINOR_GC
#define caml_minor_heap_size   ctx->caml_minor_heap_size
#define caml_young_base   ctx->caml_young_base
#define caml_young_start   ctx->caml_young_start
#define caml_young_end   ctx->caml_young_end
#define caml_young_ptr   ctx->caml_young_ptr
#define caml_young_limit   ctx->caml_young_limit
#define caml_ref_table   ctx->caml_ref_table
#define caml_weak_ref_table   ctx->caml_weak_ref_table
#define caml_in_minor_collection   ctx->caml_in_minor_collection
#define oldify_todo_list         ctx->oldify_todo_list
#endif

#ifdef CAML_CONTEXT_MEMORY
#define caml_page_table       ctx->caml_page_table
#define caml_page_table_empty ctx->caml_page_table_empty
#endif

#ifdef CAML_CONTEXT_ROOTS
#ifdef NATIVE_CODE

#define caml_local_roots          ctx->caml_local_roots
//#define caml_scan_roots_hook      ctx->caml_scan_roots_hook

#define caml_top_of_stack         ctx->caml_top_of_stack
#define caml_bottom_of_stack      ctx->caml_bottom_of_stack
#define caml_last_return_address  ctx->caml_last_return_address
#define caml_gc_regs              ctx->caml_gc_regs
#define caml_globals_inited       ctx->caml_globals_inited
#define caml_globals_scanned      ctx->caml_globals_scanned
#define caml_dyn_globals          ctx->caml_dyn_globals

#define caml_stack_usage_hook     ctx->caml_stack_usage_hook

#else
#define caml_local_roots     ctx->caml_local_roots
//#define caml_scan_roots_hook ctx->caml_scan_roots_hook
#endif
#endif

#ifdef CAML_CONTEXT_GLOBROOTS
#define random_seed             ctx->random_seed
#define caml_global_roots       ctx->caml_global_roots
#define caml_global_roots_young ctx->caml_global_roots_young
#define caml_global_roots_old   ctx->caml_global_roots_old
#endif

#ifdef CAML_CONTEXT_FAIL
#ifdef NATIVE_CODE
#define caml_exception_pointer ctx->caml_exception_pointer
#define array_bound_error_bucket_inited ctx->array_bound_error_bucket_inited
#else
#define caml_exn_bucket       ctx->caml_exn_bucket
#define caml_external_raise   ctx->caml_external_raise
#define out_of_memory_bucket  ctx->out_of_memory_bucket
#endif
#endif

#ifdef CAML_CONTEXT_SIGNALS_BYT
#ifndef NATIVE_CODE
#define caml_something_to_do    ctx->caml_something_to_do
#define caml_async_action_hook  ctx->caml_async_action_hook
#endif
#endif

#ifdef CAML_CONTEXT_SIGNALS
#define caml_signals_are_pending ctx->caml_signals_are_pending
#define caml_pending_signals     ctx->caml_pending_signals
#define caml_async_signal_mode   ctx->caml_async_signal_mode
#define caml_enter_blocking_section_hook ctx->caml_enter_blocking_section_hook
#define caml_leave_blocking_section_hook ctx->caml_leave_blocking_section_hook
#define caml_try_leave_blocking_section_hook ctx->caml_try_leave_blocking_section_hook
#define caml_force_major_slice   ctx->caml_force_major_slice
#define caml_signal_handlers     ctx->caml_signal_handlers
#endif


#ifdef CAML_CONTEXT_BACKTRACE
#ifdef NATIVE_CODE

#define caml_backtrace_active   ctx->caml_backtrace_active
#define caml_backtrace_pos      ctx->caml_backtrace_pos
#define caml_backtrace_buffer   ctx->caml_backtrace_buffer
#define caml_backtrace_last_exn ctx->caml_backtrace_last_exn

#else
#define caml_backtrace_active    ctx->caml_backtrace_active
#define caml_backtrace_pos       ctx->caml_backtrace_pos
#define caml_backtrace_buffer    ctx->caml_backtrace_buffer
#define caml_backtrace_last_exn  ctx->caml_backtrace_last_exn
#define caml_cds_file            ctx->caml_cds_file
#endif
#endif

#ifdef CAML_CONTEXT_COMPARE
#define compare_stack_init       ctx->compare_stack_init
#define compare_stack            ctx->compare_stack
#define compare_stack_limit      ctx->compare_stack_limit
#define caml_compare_unordered   ctx->caml_compare_unordered
#endif

#ifdef CAML_CONTEXT_SYS
#define caml_exe_name    ctx->caml_exe_name
#define caml_main_argv   ctx->caml_main_argv
#endif

#ifdef CAML_CONTEXT_EXTERN
#define obj_counter            ctx->obj_counter
#define size_32                ctx->size_32
#define size_64                ctx->size_64
#define extern_ignore_sharing  ctx->extern_ignore_sharing
#define extern_closures        ctx->extern_closures
#define extern_cross_context   ctx->extern_cross_context
#define extern_trail_first     ctx->extern_trail_first
#define extern_trail_block     ctx->extern_trail_block
#define extern_trail_cur       ctx->extern_trail_cur
#define extern_trail_limit     ctx->extern_trail_limit
#define extern_userprovided_output     ctx->extern_userprovided_output
#define extern_ptr     ctx->extern_ptr
#define extern_limit     ctx->extern_limit
#define extern_output_first     ctx->extern_output_first
#define extern_output_block     ctx->extern_output_block
#define extern_stack_init       (ctx->extern_stack_init)
#define extern_stack            ctx->extern_stack
#define extern_stack_limit      ctx->extern_stack_limit
#define extern_flags            ctx->extern_flags
#endif

#ifdef CAML_CONTEXT_INTERN
#define intern_src             ctx->intern_src
#define intern_input           ctx->intern_input
#define intern_input_malloced  ctx->intern_input_malloced
#define intern_dest            ctx->intern_dest
#define intern_extra_block     ctx->intern_extra_block
#define obj_counter            ctx->obj_counter
#define intern_obj_table       ctx->intern_obj_table
#define intern_color           ctx->intern_color
#define intern_header          ctx->intern_header
#define intern_block           ctx->intern_block
#define camlinternaloo_last_id ctx->camlinternaloo_last_id
#endif

/* caml_code_fragments_table is used both in extern.c and in intern.c;
   maybe these feature macros should refer header files, rather than C
   files.  No big deal anyway.  --Luca Saiu REENTRANTRUNTIME */
#if defined(CAML_CONTEXT_EXTERN) || defined(CAML_CONTEXT_INTERN) || defined(CAML_CODE_FRAGMENT_TABLE)
#define caml_code_fragments_table ctx->caml_code_fragments_table
#endif

#ifdef CAML_CONTEXT_STACKS
#define caml_global_data       ctx->caml_global_data
#ifndef caml_stack_usage_hook
#define caml_stack_usage_hook  ctx-> caml_stack_usage_hook
#endif
#define caml_stack_low         ctx->caml_stack_low
#define caml_stack_high   ctx->caml_stack_high
#define caml_stack_threshold   ctx->caml_stack_threshold
#define caml_extern_sp   ctx->caml_extern_sp
#define caml_trapsp   ctx->caml_trapsp
#define caml_trap_barrier   ctx->caml_trap_barrier
#define caml_max_stack_size   ctx->caml_max_stack_size

#endif

#ifdef CAML_CONTEXT_STARTUP
  /* from startup.c */
#define caml_atom_table       ctx->caml_atom_table
#ifdef NATIVE_CODE
// FIXME: I'm quite sure that moving these here was wrong --Luca Saiu REENTRANTRUNTIME
#define caml_code_area_start  ctx->caml_code_area_start
#define caml_code_area_end  ctx->caml_code_area_end

#define caml_termination_jmpbuf ctx->caml_termination_jmpbuf
#define caml_termination_hook   ctx->caml_termination_hook

#endif

#endif

#ifdef CAML_CONTEXT_COMPACT
#define compact_fl ctx->compact_fl
#endif

#ifdef CAML_CONTEXT_GC_CTRL
#ifndef NATIVE_CODE
#define caml_max_stack_size          ctx->caml_max_stack_size
#endif
#define caml_stat_minor_words          ctx->caml_stat_minor_words
#define caml_stat_promoted_words          ctx->caml_stat_promoted_words
#define caml_stat_major_words          ctx->caml_stat_major_words
#define caml_stat_minor_collections          ctx->caml_stat_minor_collections
#define caml_stat_major_collections          ctx->caml_stat_major_collections
#define caml_stat_heap_size          ctx->caml_stat_heap_size
#define caml_stat_top_heap_size          ctx->caml_stat_top_heap_size
#define caml_stat_compactions          ctx->caml_stat_compactions
#define caml_stat_heap_chunks          ctx->caml_stat_heap_chunks
#define caml_percent_max             ctx->caml_percent_max
#endif

#ifdef CAML_CONTEXT_CALLBACK
#define caml_callback_depth        ctx->caml_callback_depth
#ifdef THREADED_CODE
#define callback_code_threaded     ctx->callback_code_threaded
#endif
#define named_value_table          ctx->named_value_table

#endif


#ifdef CAML_CONTEXT_DEBUGGER
#define caml_debugger_in_use    ctx->caml_debugger_in_use
#define caml_event_count        ctx->caml_event_count
#define caml_debugger_fork_mode ctx->caml_debugger_fork_mode
#define marshal_flags           ctx->marshal_flags
#endif

#ifdef CAML_CONTEXT_WEAK
#define caml_weak_list_head ctx->caml_weak_list_head
#endif

#ifdef CAML_CONTEXT_FINALISE
#define final_table     ctx->final_table
#define final_old     ctx->final_old
#define final_young     ctx->final_young
#define final_size      ctx->final_size
#define to_do_hd     ctx->to_do_hd
#define to_do_tl     ctx->to_do_tl
#define running_finalisation_function ctx->running_finalisation_function

#endif


#ifdef CAML_CONTEXT_DYNLINK
#ifndef NATIVE_CODE
#define caml_prim_table ctx->caml_prim_table
#ifdef DEBUG
#define caml_prim_name_table ctx->caml_prim_name_table
#endif
#define caml_shared_libs ctx->shared_libs
#define caml_shared_libs_path ctx->caml_shared_libs_path
#endif
#endif

#ifdef CAML_CONTEXT_PARSING
#define caml_parser_trace ctx->caml_parser_trace
#endif

#ifdef CAML_CONTEXT_FIX_CODE
#define caml_start_code ctx->caml_start_code
#define caml_code_size ctx->caml_code_size
#define caml_saved_code ctx->caml_saved_code
#ifdef THREADED_CODE
#define caml_instr_table ctx->caml_instr_table
#define caml_instr_base ctx->caml_instr_base
#endif /* #ifdef THREADED_CODE */
#endif /* #ifdef CAML_CONTEXT_FIX_CODE */

#ifdef CAML_ST_POSIX_CODE
#define all_threads ctx->all_threads
#define curr_thread ctx->curr_thread
#define caml_master_lock ctx->caml_master_lock
#define caml_tick_thread_running ctx->caml_tick_thread_running
#define caml_tick_thread_id ctx->caml_tick_thread_id
#endif /* #ifdef CAML_ST_POSIX_CODE */

/* OCaml context-local "globals" */

/* /\* Reserve space for the given number of globals, resizing the global */
/*    array if needed.  Return the index of the new first element of the */
/*    new block. *\/ */
/* int caml_allocate_caml_globals_r(CAML_R, size_t added_caml_global_no); */

/* Scan all OCaml globals as roots: */
void caml_scan_caml_globals_r(CAML_R, scanning_action f);


/* C context-local "globals" */

/* Each variable is associated to a unique indentifier.  This is
   actually an offset within a context-local dynamically-allocated
   buffer.  Each variable is visilble the context given as parameter
   to caml_define_context_local_c_variable_r and all the contexts
   forked from it.  Of course ids are not guaranteed to be portable
   across architectures. */
typedef ptrdiff_t caml_c_global_id;

/* Make a context-local C variable, and return its it: */
caml_c_global_id caml_define_context_local_c_variable_r(CAML_R, size_t added_bytes);

/* Return a pointer to the context-local C variable whose ID is given: */
void* caml_context_local_c_variable_r(CAML_R, caml_c_global_id id);

/* Register an OCaml module, allocating space for its globals in the
   given context.  This should be called for each module and each context using it,
   from the camlMODULE_entry routine. */
void caml_register_module_r(CAML_R, size_t size_in_bytes, long *offset_pointer);

/* Acquire or release a global mutex: */
void caml_acquire_global_lock(void);
void caml_release_global_lock(void);

// FIXME: remove this after debugging
void caml_dump_global_mutex(void);

/* Utility: */
void caml_initialize_mutex(pthread_mutex_t *mutex);
void caml_finalize_mutex(pthread_mutex_t *mutex);
void caml_initialize_semaphore(sem_t *semaphore, int initial_value);
void caml_finalize_semaphore(sem_t *semaphore);
#define NOATTR "\033[0m"
#define RED    NOATTR "\033[31m"
#define GREEN  NOATTR "\033[32m"
#define CYAN   NOATTR "\033[36m"
#define PURPLE NOATTR "\033[35m"

#define DUMP(FORMAT, ...) \
  do{ \
    fprintf(stderr, \
            "%s:%i" NOATTR "(" RED  "%s" NOATTR ") C%p T%p AP"PURPLE"%p"NOATTR"/"PURPLE"%p"NOATTR" "CYAN"[%i threads]: %p"NOATTR, \
            __FILE__, __LINE__, __FUNCTION__, ctx, \
            (void*)pthread_self(), \
            ctx->caml_young_ptr, ctx->caml_young_limit, \
            caml_get_thread_no_r(ctx), \
            ctx->curr_thread); \
    fflush(stderr); \
    fprintf(stderr, " " GREEN FORMAT, ##__VA_ARGS__); \
    fprintf(stderr, NOATTR "\n"); \
    fflush(stderr); \
  } while(0)

/* #undef DUMP */
/* #define DUMP(FORMAT, ...) /\* nothing *\/ */

int caml_get_thread_no_r(CAML_R);
void caml_set_caml_get_thread_no_r(CAML_R, int (*f)(CAML_R));

/* Initialize thread support for the current context.  This must be
   called once at context creation time. */
void caml_initialize_context_thread_support(CAML_R);
void caml_set_caml_initialize_context_thread_support(CAML_R, void (*caml_initialize_context_thread_support)(CAML_R));

/* Return non-zero iff the given context can be split: */
int caml_can_split_r(CAML_R);
void caml_set_caml_can_split_r(CAML_R, int (*caml_can_split_r)(CAML_R));

#endif
