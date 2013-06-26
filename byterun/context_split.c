/* Luca Saiu, REENTRANTRUNTIME */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h> // FIXME: remove if not used in the end
#include <assert.h> // FIXME: remove if not used in the end

#define CAML_CONTEXT_ROOTS /* GC-protection macros */
#include "mlvalues.h"
#include "gc.h"
#include "startup.h"
#include "config.h"
#include "signals.h"
#include "memory.h"
#include "fail.h"
#include "callback.h" // for caml_callback_r and friends
#include "alloc.h"
#include "intext.h"
#include "fix_code.h"
#include "printexc.h" // FIXME: remove after debugging, if possible

#include "gc_ctrl.h" // FIXME: remove after debugging, if possible
#include "compact.h" // FIXME: remove after debugging, if possible

struct caml_mailbox* caml_make_mailbox_r(CAML_R){
  struct caml_mailbox *m = caml_stat_alloc(sizeof(struct caml_mailbox));
  m->descriptor = ctx->descriptor;
  caml_initialize_mutex(&m->mutex);
  //caml_initialize_semaphore(&m->free_slot_no_semaphore, MESSAGE_QUEUE_SIZE);
  caml_initialize_semaphore(&m->message_no_semaphore, 0);
  //caml_initialize_semaphore(&m->free_slot_no_semaphore, CAML_QUEUE_SLOT_NO);
  m->message_queue = caml_stat_alloc(sizeof(struct caml_message) * CAML_INITIAL_ALLOCATED_MESSAGE_NO);
  m->allocated_message_no = CAML_INITIAL_ALLOCATED_MESSAGE_NO;
  m->message_no = 0;
  //fprintf(stderr, "caml_make_mailbox_r [%p]: made m %p\n", ctx, m); fflush(stderr);

  return m;
}

CAMLprim value caml_camlprim_make_mailbox_r(CAML_R){
  return caml_value_of_mailbox(caml_make_mailbox_r(ctx));
}

CAMLprim value caml_camlprim_context_of_mailbox_r(CAML_R, value mailbox_as_value){
  CAMLparam1(mailbox_as_value);
  struct caml_mailbox *m = caml_mailbox_of_value(mailbox_as_value);

  CAMLreturn(caml_value_of_context_descriptor(m->descriptor));
}

void caml_destroy_local_mailbox_r(CAML_R, struct caml_mailbox *mailbox){
  /* Destroy all blobs still in the queue, if any: */
  int i;
  for(i = 0; i < mailbox->message_no; i ++)
    free(mailbox->message_queue[i].message_blob);
  free(mailbox->message_queue);

  caml_finalize_mutex(&mailbox->mutex);
  caml_finalize_semaphore(&mailbox->message_no_semaphore);
  //caml_finalize_semaphore(&mailbox->free_slot_no_semaphore);
  free(mailbox);
}

/* /\* We implement a slightly more general facility than what is declared */
/*    in the header.  Each serialized context contains globals, plus a */
/*    tuple of values which may share pointers (not necessarily one */
/*    single closure). *\/ */

/* static value caml_tuple_of_c_array_r(CAML_R, value *array, size_t element_no) __attribute__((unused)); */
/* static value caml_tuple_of_c_array_r(CAML_R, value *array, size_t element_no) */
/* { */
/*   CAMLparam0(); */
/*   CAMLlocal1(result); */
/*   result = caml_alloc_tuple_r(ctx, element_no); */
/*   int i; */
/*   for(i = 0; i < element_no; i ++){ */
/*     if(array[i] == 0) */
/*       fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th array element is zero!\n", ctx, i); */
/*     caml_initialize_r(ctx, &Field(result, i), array[i]); */
/*   } */
/*   CAMLreturn(result); */
/* } */

static void caml_copy_tuple_elements_r(CAML_R, value *to_array, size_t *to_element_no, value from_tuple) __attribute__((unused)) /* only needed for the native runtime */;
static void caml_copy_tuple_elements_r(CAML_R, value *to_array, size_t *to_element_no, value from_tuple)
{
  CAMLparam1(from_tuple);
  size_t element_no = Wosize_val(from_tuple);
  *to_element_no = element_no;
  int i;
  for(i = 0; i < element_no; i ++){
    if(Field(from_tuple, i) == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th tuple element is zero!\n", ctx, i);
    to_array[i] = Field(from_tuple, i);
  }
  CAMLreturn0;
}

static value caml_pair_r(CAML_R, value left, value right)
{
  CAMLparam2(left, right);
  CAMLlocal1(result);
  result = caml_alloc_tuple_r(ctx, 2);
  caml_initialize_r(ctx, &Field(result, 0), left);
  caml_initialize_r(ctx, &Field(result, 1), right);
  CAMLreturn(result);
}

static value caml_triple_r(CAML_R, value a, value b, value c)
{
  CAMLparam3(a, b, c);
  CAMLlocal1(result);
  result = caml_alloc_tuple_r(ctx, 3);
  caml_initialize_r(ctx, &Field(result, 0), a);
  caml_initialize_r(ctx, &Field(result, 1), b);
  caml_initialize_r(ctx, &Field(result, 2), c);
  CAMLreturn(result);
}

/* Return a Caml tuple/array containing all the globals of the given
   context.  The result should not be modified as it may share
   structure with the context globals.  The result may be invalidated
   by loading more caml compilation units. */
CAMLprim value caml_global_array_r(CAML_R, value unit)
{
  CAMLparam0();
#ifdef NATIVE_CODE
  CAMLlocal1(globals);
  const int global_no = ctx->caml_globals.used_size / sizeof(value);
  globals = caml_alloc_tuple_r(ctx, global_no);
  int i;
  for(i = 0; i < global_no; i ++){
    if(((value*)ctx->caml_globals.array)[i] == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th global is zero!\n", ctx, i);
    caml_initialize_r(ctx, &Field(globals, i), ((value*)ctx->caml_globals.array)[i]);
  }
  int element_no = Wosize_val(globals);
  assert(element_no == global_no);
  //fprintf(stderr, "[native] The tuple has %i elements; it should be %i\n", (int)element_no, (int)global_no);

  CAMLreturn(globals);
#else /* bytecode */
  /* No need for GC-protection: there is no allocation here. */
  // FIXME: for debugging only.  Remove: BEGIN
  //globals = ctx->caml_global_data;
  //int element_no = Wosize_val(globals);
  //fprintf(stderr, "[bytecode] The tuple has %i elements\n", (int)element_no);
  // FIXME: for debugging only.  Remove: END

  CAMLreturn(ctx->caml_global_data);
#endif /* #else, #ifdef NATIVE_CODE */
}

/* Replace the globals of the given context with the elements of the given tuple: */
void caml_set_globals_r(CAML_R, value global_tuple){
  /* No need to GC-protect anything here: we do not allocate anything
     from the Caml heap, in either branch. */
#ifdef NATIVE_CODE
  size_t global_tuple_size = Wosize_val(global_tuple);
  //fprintf(stderr, "caml_set_globals_r: there are %i globals to be copied\n", (int)global_tuple_size);
  caml_resize_extensible_buffer(&ctx->caml_globals,
                                global_tuple_size * sizeof(value),
                                1);
  void* to_globals = ctx->caml_globals.array;
  size_t to_global_no;
  caml_copy_tuple_elements_r(ctx,
                             to_globals, &to_global_no,
                             global_tuple);
  assert(to_global_no == global_tuple_size);
  //fprintf(stderr, "TTTTTTTTTTT: there are now %i globals in the child context\n", (int)(ctx->caml_globals.used_size / sizeof(value)));
#else /* bytecode */
  ctx->caml_global_data = global_tuple;
  // FIXME: is this needed?  It might be.  It's in startup.c, right after loading
  // constants. --Luca Saiu REENTRANTRUNTIME
  //caml_oldify_one_r(ctx, ctx->caml_global_data, &ctx->caml_global_data);
  //caml_oldify_mopup_r(ctx);
#endif /* #else, #ifdef NATIVE_CODE */
}

static value caml_globals_and_data_r(CAML_R, value function)
{
  CAMLparam1(function);
  CAMLlocal1(globals);
  globals = caml_global_array_r(ctx, Val_unit);
  DUMP("ctx->caml_signal_handlers is %p", (void*)(long)(ctx->caml_signal_handlers));
  CAMLreturn(caml_triple_r(ctx, globals, function, ctx->caml_signal_handlers));
}

/* Return a pointer to a malloc'ed buffer: */
//static long QQQ_length;
char* caml_serialize_into_blob_r(CAML_R, value caml_value){
  CAMLparam1(caml_value);
  CAMLlocal1(flags);
  char *blob;
  intnat blob_length;

  flags = /* Marshal.Closures :: Marshal.Cross_context :: [] */
//caml_pair_r(ctx, Val_int(0), /* Marshal.No_sharing, 1st constructor */
    caml_pair_r(ctx,
                Val_int(1), /* Marshal.Closures, 2nd constructor */
                caml_pair_r(ctx,
                            Val_int(2), /* Marshal.Cross_context, 3rd constructor */
                            Val_emptylist))
//)
    ;

  /* Marshal the big data structure into a byte array: */
  //caml_acquire_global_lock(); // FIXME: I should be able to remove this RIGHT NOW: do it when the thing is stable
//  pthread_mutex_lock(& ctx->mutex);
//caml_acquire_contextual_lock(ctx);
  caml_output_value_to_malloc_r(ctx, caml_value, flags, &blob, &blob_length);
//caml_release_contextual_lock(ctx);
  //  pthread_mutex_unlock(& ctx->mutex);
  //caml_release_global_lock(); // FIXME: I should be able to remove this RIGHT NOW: do it when the thing is stable
//fprintf(stderr, "Ok-Q 100: ...serialized a structure into the blob at %p (length %.2fMB).\n", blob, blob_length / 1024. / 1024.); fflush(stderr);
//DUMP("Made a %fMB blob at %p", blob_length / 1024. / 1024., blob);
//  QQQ_length = blob_length;

  CAMLreturnT(char*, blob);
}

value caml_input_value_from_string_r(CAML_R, value, value);

value caml_deserialize_blob_r(CAML_R, char *blob){
  CAMLparam0();
  CAMLlocal1(result);
  //CAMLlocal2(result, blob_as_caml_string);
  //caml_acquire_global_lock(); // FIXME: I should be able to remove this RIGHT NOW: do it when the thing is stable
//DUMP("Deserializing the blob at %p", blob);
  //pthread_mutex_lock(& ctx->mutex);
//caml_acquire_contextual_lock(ctx);
  result = caml_input_value_from_block_r(ctx,
                                         blob,
                                         /* FIXME: this third parameter is useless in practice: discuss with the
                                            OCaml people to provide an alternate version of
                                            caml_input_value_from_block_r with two parameters.  I don't want to
                                            mess up the interface myself, since I'm doing a lot of other invasive
                                            changes --Luca Saiu REENTRANTRUNTIME */
                                         LONG_MAX);
//caml_release_contextual_lock(ctx);
  //pthread_mutex_unlock(& ctx->mutex);
  //blob_as_caml_string = caml_alloc_string_r(ctx, QQQ_length);
  //memmove(String_val(blob_as_caml_string), blob, QQQ_length);

  //DUMP("Deserialized with success");
  //caml_release_global_lock(); // FIXME: I should be able to remove this RIGHT NOW: do it when the thing is stable
  CAMLreturn(result);
}

/* Of course the result is malloc'ed. */
static char* caml_globals_and_data_as_c_byte_array_r(CAML_R, value function){
  /* Make a big structure holding all globals and user-specified data, and marshal it into a blob: */
  return caml_serialize_into_blob_r(ctx, caml_globals_and_data_r(ctx, function));
}

static void caml_install_globals_and_data_as_c_byte_array_r(CAML_R, char *blob, value *function){
  CAMLparam0();
  CAMLlocal1(globals_and_data);
  //value globals_and_data, global_tuple, data_tuple;
  size_t to_value_no __attribute__((unused));
  //fprintf(stderr, "Ok-A 100\n");

  /* Deserialize globals and data from the byte array, and access each
     element of the pair. */
  //fprintf(stderr, "Context %p: L0 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
  globals_and_data = caml_deserialize_blob_r(ctx, blob);

  //fprintf(stderr, "Context %p: L1 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
    //caml_input_value_from_malloc_r(ctx, globals_and_data_as_c_array, 0); // this also frees the buffer */
  /* global_tuple = Field(globals_and_data, 0); */
  /* data_tuple = Field(globals_and_data, 1); */
  //fprintf(stderr, "Context %p: L2 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);

  /* Replace the context globals with what we got: */
  caml_set_globals_r(ctx, Field(globals_and_data, 0));
  //fprintf(stderr, "Context %p: L3 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);

  /* /\* Copy deserialized data from the tuple to the given address; the */
  /*    tuple will be GC'd: *\/ */
  /* caml_copy_tuple_elements_r(ctx, */
  /*                            to_values, &to_value_no, */
  /*                            data_tuple); */
  *function = Field(globals_and_data, 1);
  ctx->caml_signal_handlers = Field(globals_and_data, 2);
  DUMP("ctx->caml_signal_handlers is %p", (void*)(long)(ctx->caml_signal_handlers));
  //fprintf(stderr, "Context %p: L4 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
  //fprintf(stderr, "Ok-A 600 (the tuple has %i elements)\n", (int)to_value_no);
  CAMLreturn0;
}

/* Implement the interface specified in the header file. */

/* struct caml_context_blob{ */
/*   char *data; */
/*   int reference_count; */
/* }; /\* struct *\/ */

static char* caml_serialize_context(CAML_R, value function) __attribute__((unused)); // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
static char* caml_serialize_context(CAML_R, value function)
{
  CAMLparam1(function);
  char *result = caml_globals_and_data_as_c_byte_array_r(ctx, function);
  CAMLreturnT(char*, result);
}

/* Return 0 on success and non-zero on failure. */
static int caml_run_function_this_thread_r(CAML_R, value function, int index)
{
  CAMLparam1(function);
  CAMLlocal1(result_or_exception);
  int did_we_fail;

/* fprintf(stderr, "======Forcing a GC\n"); fflush(stderr); */
//caml_gc_compaction_r(ctx, Val_unit); //!!!!!
/* fprintf(stderr, "======It's ok to have warnings about the lack of globals up to this point\n"); fflush(stderr); */

//fprintf(stderr, "W0[context %p] [thread %p] (index %i) BBBBBBBBBBBBBBBBBBBBBBBBBB\n", ctx, (void*)(pthread_self()), index); fflush(stderr); caml_acquire_global_lock(); // FIXME: a test. this is obviously unusable in production
//fprintf(stderr, "W1 [context %p] ctx->caml_local_roots is %p\n", ctx, caml_local_roots); fflush(stderr);
//DUMP();
  /* Make a new context, and deserialize the blob into it: */
  /* fprintf(stderr, "W3 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr); */

  /* // Allocate some trash: */
  /* caml_pair_r(ctx, */
  /*             caml_pair_r(ctx, Val_int(1), Val_int(2)), */
  /*             caml_pair_r(ctx, Val_int(3), Val_int(4))); */

 //fprintf(stderr, "W4 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);
//caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  //DUMP();

/* caml_empty_minor_heap_r(ctx); */
/* caml_finish_major_cycle_r (ctx); */
/* caml_compact_heap_r (ctx); */
/* caml_final_do_calls_r (ctx); */

  /* Run the Caml function: */
 //fprintf(stderr, "W5 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);
  //caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  //DUMP();
  //fprintf(stderr, "W7 [context %p] [thread %p] (index %i) (%i globals) ctx->caml_local_roots is %p\n", ctx, (void*)(pthread_self()), index, (int)(ctx->caml_globals.used_size / sizeof(value)), caml_local_roots); fflush(stderr);
  //caml_dump_global_mutex();

  /* It's important that Extract_exception be used before the next
     collection, because result_or_exception is an invalid value in
     case of exception: */
  DUMP("running caml code in the new context");
  result_or_exception = caml_callback_exn_r(ctx, function, Val_int(index));
  DUMP("back from the caml code in the new context");
  /* If we decide to actually do something with result_or_exception,
     then it becomes important that we call Extract_exception on it
     (when it's an exception) before the next Caml allocation: in case
     of exception result_or_exception is an invalid value, messing up
     the GC. */
  did_we_fail = Is_exception_result(result_or_exception);
  if(did_we_fail){
    result_or_exception = Extract_exception(result_or_exception);
    char *printed_exception = caml_format_exception_r(ctx, result_or_exception);
    fprintf(stderr, "FAILED with the exception %s\n", printed_exception); fflush(stderr);
    free(printed_exception);
  }
  CAMLreturnT(int, did_we_fail);
}

/* Return 0 on success and non-zero on failure. */
static int caml_deserialize_and_run_in_this_thread(caml_global_context *parent_context, char *blob, int index, sem_t *semaphore, /*out*/caml_global_context **to_context)
{
  CAML_R = caml_make_empty_context(); // ctx also becomes the thread-local context
  /* DUMP("sleeping %i seconds", index); */
  /* sleep(index); */
  /* DUMP("slept %i seconds", index); */
  /* Make a new empty context, and use it to deserialize the blob
     into. */
  CAMLparam0();
  CAMLlocal1(function);
  int did_we_fail;

  caml_initialize_context_thread_support_r(ctx);
  // !!!!!! NEW STUFF ADDED: BEGIN !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  ctx->caml_start_code = parent_context->caml_start_code;
  ctx->caml_code_size = parent_context->caml_code_size;
  ctx->caml_saved_code = parent_context->caml_saved_code;
#ifdef THREADED_CODE
  ctx->caml_instr_table = parent_context->caml_instr_table;
  ctx->caml_instr_base = parent_context->caml_instr_base;
#endif // #ifdef THREADED_CODE

#ifndef NATIVE_CODE
  DUMP();
  caml_init_code_fragments_r(ctx); // this is needed for caml_install_globals_and_data_as_c_byte_array_r
  DUMP();
  ctx->caml_prim_table = parent_context->caml_prim_table;
  ctx->caml_prim_name_table = parent_context->caml_prim_name_table;
#endif // #ifdef THREADED_CODE

  *to_context = ctx;
  caml_install_globals_and_data_as_c_byte_array_r(ctx, blob, &function);

  /* We're done with the blob: unpin it via the semaphore, so that it
     can be destroyed when all split threads have deserialized. */
//fprintf(stderr, "W5.5context %p] [thread %p] (index %i) EEEEEEEEEEEEEEEEEEEEEEEEEE\n", ctx, (void*)(pthread_self()), index); fflush(stderr); caml_release_global_lock();
  DUMP("About to V the semaphore.  index=%i\n", index);
  sem_post(semaphore);

#ifndef NATIVE_CODE
  DUMP();
  caml_init_exceptions_r(ctx);
  DUMP();
  //caml_debugger_r(ctx, PROGRAM_START);
#endif // #ifndef NATIVE_CODE
  DUMP();
  ctx->caml_exe_name = parent_context->caml_exe_name;
  ctx->caml_main_argv = parent_context->caml_main_argv;
  DUMP();
  // !!!!!! NEW STUFF ADDED: END !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  /* Now do the actual work, in a function which correctly GC-protects its locals: */
  did_we_fail = caml_run_function_this_thread_r(ctx, function, index);
  if(did_we_fail){
    fprintf(stderr, "caml_deserialize_and_run_in_this_thread [context %p] [thread %p] (index %i).  FAILED.\n", ctx, (void*)(pthread_self()), index); fflush(stderr);
    DUMP("the Caml code failed"); // !!!!!!!!!!!!!!!!!!!!!!!!!!! What shall we do in this case?
    volatile int a = 1; a /= 0; /*die horribly*/
  }
  /* We're done.  But we can't destroy the context yet, until it's
     joined: the object must remain visibile to the OCaml code, and
     for accessing the pthread_t objecet from the C join code. */
  CAMLreturnT(int, did_we_fail);
}

struct caml_thread_arguments{
  caml_global_context *parent_context;
  char *blob;
  sem_t *semaphore;
  caml_global_context **split_contexts;
  int index;
}; /* struct */

static void* caml_deserialize_and_run_in_this_thread_as_thread_function(void *args_as_void_star)
{
  struct caml_thread_arguments *args = args_as_void_star;
  int did_we_fail = caml_deserialize_and_run_in_this_thread(args->parent_context, args->blob, args->index, args->semaphore, args->split_contexts + args->index);
  //fprintf(stderr, "caml_deserialize_and_run_in_this_thread_as_thread_function (index %i) [about to free args].  Did we fail? %i\n", args->index, did_we_fail); fflush(stderr);
  caml_stat_free(args);
  return (void*)(long)did_we_fail;
}

/* Create threads, and wait until all of them have signaled that they're done with the blob: */
static void caml_split_and_wait_r(CAML_R, char *blob, caml_global_context **split_contexts, size_t how_many, sem_t *semaphore) __attribute__((unused)); // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
static void caml_split_and_wait_r(CAML_R, char *blob, caml_global_context **split_contexts, size_t how_many, sem_t *semaphore)
{
  //DUMP();
  //#ifdef NATIVE_CODE
  //  fprintf(stderr, "@@@@@ In the parent context caml_bottom_of_stack is %p\n", caml_bottom_of_stack);
  //#endif // #ifdef NATIVE_CODE
  //DUMP();
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  //DUMP();
  int i;
  for(i = 0; i < how_many; i ++){
    //sleep(10); // FIXME: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    pthread_t thread;
    struct caml_thread_arguments *args = caml_stat_alloc(sizeof(struct caml_thread_arguments));
    int pthread_create_result;
    args->parent_context = ctx;
    args->blob = blob;
    args->semaphore = semaphore;
    args->split_contexts = split_contexts;
    args->index = i;

    pthread_create_result =
      pthread_create(&thread, NULL, caml_deserialize_and_run_in_this_thread_as_thread_function, args);
    if(pthread_create_result != 0)
      caml_failwith_r(ctx, "pthread_create failed"); // FIXME: blob is leaked is this case.  Maybe we should just make this a fatal error
  } /* for */
  /* Wait for the last thread to use the blob: */
  //DUMP("waiting for every thread to deserialize");
  for(i = 0; i < how_many; i ++){
    DUMP("about to P");
caml_enter_blocking_section_r(ctx);
    sem_wait(semaphore);
caml_leave_blocking_section_r(ctx);
    DUMP("one child finished; waiting for %i more", (int)(how_many - i - 1));
  }
  DUMP("every thread has deserialized");
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  /* #define MAXK 0 */
  /* int k; for(k = MAXK; k > 0; k --) { sleep(1); DUMP("countdown: %i", k); DUMP("GC'ing"); caml_gc_compaction_r(ctx, Val_unit); DUMP("GC'd"); } */
  /* // ???????????? Re-activate the following line when testing */
  /* DUMP("the countdown is over"); */
  //USLEEP("", 3);
  DUMP("GC'ing"); caml_gc_compaction_r(ctx, Val_unit); DUMP("GC'd");
  //DUMP("and now we're screwed.  Aren't we?");
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
}

CAMLprim value caml_context_split_r(CAML_R, value thread_no_as_value, value function)
{
  CAMLparam1(function);
  CAMLlocal2(result, open_channels);

  value *exception_closure = caml_named_value_r(ctx, "CannotSplit");
  int can_split = caml_can_split_r(ctx);
  if (! can_split)
    caml_raise_constant_r(ctx, *exception_closure);

  int thread_no = Int_val(thread_no_as_value);
  caml_global_context **new_contexts = caml_stat_alloc(sizeof(caml_global_context*) * thread_no);
  char *blob;
  sem_t semaphore;
  int i;
  caml_initialize_semaphore(&semaphore, 0);

  /* CAMLparam0(); CAMLlocal1(open_channels); */
  /* Make sure that the currently-existing channels stay alive until
     after deserialization; we can't keep reference counts within the
     blob, so we pin all alive channels by keeping this list alive: */
  open_channels = caml_ml_all_channels_list_r(ctx); // !!!!!!!!!!!!!!!!!!!! This can occasionally cause crashes related to channel picounts.  I certainly messed up something in io.c. //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  /* Serialize the context in the main thread, then create threads,
     and in each one of them deserialize it back in parallel:  */
  blob = caml_serialize_context(ctx, function);
  caml_split_and_wait_r(ctx, blob, new_contexts, thread_no, &semaphore);

  /* Now we're done with the blob: */
  DUMP("destroying the blob");
  caml_stat_free(blob); // !!!!!!!!!!!!!!!!!!!!!!!!!!! This is needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  DUMP("GC'ing after destroying the blob");
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!@@@@@@@@@@@@@
  DUMP("finalizing the semaphore");

  caml_finalize_semaphore(&semaphore);

  /* Copy the contexts we got, and we're done with new_contexts as well: */
  DUMP("copying the new context (descriptors) into the Caml data structure result");
  result = caml_alloc_r(ctx, thread_no, 0);
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!@@@@@@@@@@@@
  for(i = 0; i < thread_no; i ++)
    caml_initialize_r(ctx, &Field(result, i), caml_value_of_context_descriptor(new_contexts[i]->descriptor));
  caml_stat_free(new_contexts);
  DUMP("destroyed the malloced buffer of pointers new_contexts");
  CAMLreturn(result);
  //CAMLreturn(Val_unit);
}

CAMLprim value caml_context_join_r(CAML_R, value context_as_value){
  struct caml_global_context_descriptor *descriptor;
  int pthread_join_result;
  void* did_we_fail_as_void_star;
  int did_we_fail;
  CAMLparam1(context_as_value);
  CAMLlocal1(result);
  descriptor = caml_global_context_descriptor_of_value(context_as_value);

  //fprintf(stderr, "!!!! ABOUT TO JOIN [descriptor %p]\n", descriptor); fflush(stderr);
  //fprintf(stderr, "!!!! ABOUT TO JOIN [kind %i]\n", descriptor->kind); fflush(stderr);
  if(descriptor->kind == caml_global_context_main)
    caml_failwith_r(ctx, "caml_context_join_r: main context");
  else if(descriptor->kind == caml_global_context_remote)
    caml_failwith_r(ctx, "caml_context_join_r: remote context");
  else if(descriptor->kind == caml_global_context_dead)
    caml_failwith_r(ctx, "caml_context_join_r: dead context");
  assert(descriptor->kind == caml_global_context_nonmain_local);
  //fprintf(stderr, "!!!! JOINING %p\n", (void*)descriptor->content.local_context.context->thread); fflush(stderr);
  pthread_join_result = pthread_join(descriptor->content.local_context.context->thread, &did_we_fail_as_void_star);
  did_we_fail = (int)(long)did_we_fail_as_void_star;
  //fprintf(stderr, "!!!! JOINED %p: did we fail? %i\n", (void*)descriptor->content.local_context.context->thread, did_we_fail); fflush(stderr);
  if(pthread_join_result != 0)
    caml_failwith_r(ctx, "caml_context_join_r: pthread_join failed");

  /* Now we will not need the context any longer, and we can finally free its resources: */
  //fprintf(stderr, "caml_context_join [context %p] [thread %p]: destroyING the context %p\n", ctx, (void*)(pthread_self()), descriptor->content.local_context.context); fflush(stderr);
  caml_destroy_context(descriptor->content.local_context.context);
  //fprintf(stderr, "caml_context_join [context %p] [thread %p]: destroyED  the context.\n", ctx, (void*)(pthread_self())); fflush(stderr);

  /* FIXME: this is probably *not* the right policy.  Freeing
     resources becomes a mess in this case. */
  if(pthread_join_result != 0)
    caml_failwith_r(ctx, "caml_context_join_r: failed");
  CAMLreturn(Val_unit);
}

CAMLprim value caml_context_send_r(CAML_R, value receiver_mailbox_as_value, value message){
  //fprintf(stderr, "SEND: OK-1\n"); fflush(stderr);
  CAMLparam2(receiver_mailbox_as_value, message);
  struct caml_mailbox *receiver_mailbox;
  char *message_blob;
  receiver_mailbox = caml_mailbox_of_value(receiver_mailbox_as_value);

  //fprintf(stderr, "caml_context_send_r    [%p, m %p]: OK-10 BEFORE P, message_no is %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-1\n", ctx, receiver_mailbox); fflush(stderr);
  /* First serialize the message; this is the slow part, and we can do
     it out of the critical section: */
  message_blob = caml_serialize_into_blob_r(ctx, message);

  //fprintf(stderr, "SEND: OK-2\n"); fflush(stderr);
  /* /\* Wait until there is a free slot: *\/ */
  /* caml_enter_blocking_section_r(ctx); */
  /* sem_wait(&receiver_mailbox->free_slot_no_semaphore); */
  /* caml_leave_blocking_section_r(ctx); */

  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-20 BEFORE LOCK\n", ctx, receiver_mailbox); fflush(stderr);
  /* Write the message into the receiver's data structure, and unblock it: */
  pthread_mutex_lock(&receiver_mailbox->mutex);
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-30 AFTER LOCK\n", ctx, receiver_mailbox); fflush(stderr);
  int message_no = receiver_mailbox->message_no;

  //fprintf(stderr, "SEND: OK-3\n"); fflush(stderr);
  /* Make sure there is enough space, enlarging the queue if needed: */
  if(message_no == receiver_mailbox->allocated_message_no){
    receiver_mailbox->allocated_message_no *= 2;
    receiver_mailbox->message_queue =
      realloc(receiver_mailbox->message_queue, sizeof(struct caml_message) * receiver_mailbox->allocated_message_no);
    //fprintf(stderr, "caml_context_send_r [%p, m %p]: doubled the messaque queue size to %i\n", ctx, receiver_mailbox, receiver_mailbox->allocated_message_no); fflush(stderr);
  } // if
  receiver_mailbox->message_queue[message_no].message_blob = message_blob;
  receiver_mailbox->message_no = message_no + 1;
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-40 BEFORE UNLOCK; message_no is now %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  pthread_mutex_unlock(&receiver_mailbox->mutex);
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-50 AFTER UNLOCK BEFORE V\n", ctx, receiver_mailbox); fflush(stderr);
  sem_post(&receiver_mailbox->message_no_semaphore);
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-60 AFTER V\n", ctx, receiver_mailbox); fflush(stderr);
  //fprintf(stderr, "caml_context_send_r [%p, m %p]: OK-100\n", ctx, receiver_mailbox); fflush(stderr);
  //fprintf(stderr, "caml_context_send_r    [%p, m %p]: OK-100 END, message_no is %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  //fprintf(stderr, "SEND: OK-4\n"); fflush(stderr);

  CAMLreturn(Val_unit);
}

CAMLprim value caml_context_receive_r(CAML_R, value receiver_mailbox_as_value){
  //fprintf(stderr, "RECEIVE: OK-1\n"); fflush(stderr);
  CAMLparam1(receiver_mailbox_as_value);
  CAMLlocal1(message);
  struct caml_mailbox *receiver_mailbox = caml_mailbox_of_value(receiver_mailbox_as_value);
  char *message_blob;
  //fprintf(stderr, "caml_context_receive_r [%p]: WAITING FOR A MESSAGE.\n", ctx); fflush(stderr);
  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-1\n", ctx, receiver_mailbox); fflush(stderr);

  /* /\* Fail if the mailbox is not local; *\/ */
  /* if(ctx->descriptor != receiver_mailbox->descriptor) */
  /*   caml_failwith_r(ctx, "foreign mailbox"); */

  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-10 BEFORE P, message_no is %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  /* Wait until there is a message: */
  caml_enter_blocking_section_r(ctx);
  sem_wait(&receiver_mailbox->message_no_semaphore);
  caml_leave_blocking_section_r(ctx);

  //fprintf(stderr, "RECEIVE: OK-2\n"); fflush(stderr);
  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-20 AFTER P, BEFORE LOCK\n", ctx, receiver_mailbox); fflush(stderr);
  /* Get what we need, and immediately unblock the next sender; we can
     process our message after V'ing. */
  pthread_mutex_lock(&receiver_mailbox->mutex);
  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-30 AFTER LOCK\n", ctx, receiver_mailbox); fflush(stderr);
  int message_no = receiver_mailbox->message_no;
  assert(message_no > 0);
  message_blob = receiver_mailbox->message_queue[0].message_blob;
  /* Shift the queue elements to the left by one position */
  int i; for(i = 0; i < (message_no - 1); i ++)
    receiver_mailbox->message_queue[i] = receiver_mailbox->message_queue[i + 1];
  /* Invalidate the rightmost message.  This is useful at destruction
     time, for not freeing structures more than once: */
  receiver_mailbox->message_queue[message_no - 1].message_blob = NULL; // just for debugging
  receiver_mailbox->message_no = message_no - 1;
  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-40 BEFORE UNLOCK; message_no is now %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  pthread_mutex_unlock(&receiver_mailbox->mutex);
  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-50 AFTER UNLOCK\n", ctx, receiver_mailbox); fflush(stderr);
  //fprintf(stderr, "RECEIVE: OK-3\n"); fflush(stderr);

  /* /\* Signal the fact that there one slot has been freed: *\/ */
  /* sem_post(&receiver_mailbox->free_slot_no_semaphore); */

  message = caml_deserialize_blob_r(ctx, message_blob);
  free(message_blob);

  //fprintf(stderr, "caml_context_receive_r [%p, m %p]: OK-100 END, message_no is %i\n", ctx, receiver_mailbox, (int)receiver_mailbox->message_no); fflush(stderr);
  //fprintf(stderr, "RECEIVE: OK-4\n"); fflush(stderr);

  CAMLreturn(message);
}

CAMLprim value caml_dump_r(CAML_R, value string){
  DUMP("%s", String_val(string));
  return Val_unit;
}
