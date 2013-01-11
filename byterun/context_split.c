/* Luca Saiu, REENTRANTRUNTIME */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h> // FIXME: remove if not used in the end

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

#include "gc_ctrl.h" // FIXME: remove after debugging, if possible
#include "compact.h" // FIXME: remove after debugging, if possible

/* We implement a slightly more general facility than what is declared
   in the header.  Each serialized context contains globals, plus a
   tuple of values which may share pointers (not necessarily one
   single closure). */

static value caml_tuple_of_c_array_r(CAML_R, value *array, size_t element_no)
{
  /* No need for GC protection: this is the only allocation, and if
     the GC moves the objects pointed by array at allocation time,
     that's no problem. */
  //fprintf(stderr, ">>>>>>>>>element_no is %i\n", (int)element_no);
  value result = caml_alloc_tuple_r(ctx, element_no);
  int i;
  for(i = 0; i < element_no; i ++){
    if(array[i] == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th array element is zero!\n", ctx, i);
    caml_initialize_r(ctx, &Field(result, i), array[i]);
  }
  return result;
}

static void caml_copy_tuple_elements_r(CAML_R, value *to_array, size_t *to_element_no, value from_tuple)
{
  /* No need for GC-protection: there is no allocation here. */
  size_t element_no = Wosize_val(from_tuple);
  *to_element_no = element_no;
  int i;
  for(i = 0; i < element_no; i ++){
    if(Field(from_tuple, i) == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th tuple element is zero!\n", ctx, i);
    to_array[i] = Field(from_tuple, i);
  }
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

/* Return a Caml tuple containing all the globals of the given context.  The
   result should not be modified as it may share structure with the context
   globals. */
value caml_global_tuple_r(CAML_R)
{
#ifdef NATIVE_CODE
  /* No need for GC-protection here: there is only one allocation, and
     we don't use parameters or temporaries of type value. */
  const int global_no = ctx->caml_globals.used_size / sizeof(value);
  /* This is the only allocation, and no Caml locals are alive at this
     point: no need fot GC protection: */
  value globals = caml_alloc_tuple_r(ctx, global_no);
  int i;
  for(i = 0; i < global_no; i ++){
    if(((value*)ctx->caml_globals.array)[i] == 0)
      fprintf(stderr, "%%%%%%%%%% Context %p: the %i-th global is zero!\n", ctx, i);
    caml_initialize_r(ctx, &Field(globals, i), ((value*)ctx->caml_globals.array)[i]);
  }
  int element_no = Wosize_val(globals);
  fprintf(stderr, "[native] The tuple has %i elements; it should be %i\n", (int)element_no, (int)global_no);

  return globals;
#else /* bytecode */
  /* No need for GC-protection: there is no allocation here. */
  // FIXME: for debugging only.  Remove: BEGIN
  value globals = ctx->caml_global_data;
  int element_no = Wosize_val(globals);
  fprintf(stderr, "[bytecode] The tuple has %i elements\n", (int)element_no);
  // FIXME: for debugging only.  Remove: END

  return ctx->caml_global_data;
#endif /* #else, #ifdef NATIVE_CODE */
}

CAMLprim value caml_global_array_r(CAML_R, value unit)
{
  return caml_global_tuple_r(ctx);
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
  Assert(to_global_no == global_tuple_size);
  //fprintf(stderr, "TTTTTTTTTTT: there are now %i globals in the child context\n", (int)(ctx->caml_globals.used_size / sizeof(value)));
#else /* bytecode */
  ctx->caml_global_data = global_tuple;
  // FIXME: is this needed?  It might be.  It's in startup.c, right after loading
  // constants. --Luca Saiu REENTRANTRUNTIME
  //caml_oldify_one_r(ctx, ctx->caml_global_data, &ctx->caml_global_data);
  //caml_oldify_mopup_r(ctx);
#endif /* #else, #ifdef NATIVE_CODE */
}

static value caml_globals_and_data_r(CAML_R, value *data, size_t element_no)
{
  CAMLparam0();
  CAMLlocal2(globals, values_to_clone);
  /* The GC can move the objects pointed by data at this time: no problem. */
  globals = caml_global_tuple_r(ctx);
  values_to_clone = caml_tuple_of_c_array_r(ctx, data, element_no);
  CAMLreturn(caml_pair_r(ctx, globals, values_to_clone));
}

/* The result is malloc'ed. */
static char* caml_globals_and_data_as_c_byte_array_r(CAML_R, value *data, size_t element_no){
  CAMLparam0();
  CAMLlocal2(globals_and_data, flags);
  char *serialized_tuple;
  intnat serialized_tuple_length;

  /* Make a big structure holding all globals and user-specified data: */
  globals_and_data = caml_globals_and_data_r(ctx, data, element_no);

  /* Serialize it into a malloced string, and return the string: */
  flags = /* Marshal.Closures :: Marshal.Cross_context :: [] */
//caml_pair_r(ctx, Val_int(0), /* Marshal.Closures, 1st constructor */
    caml_pair_r(ctx,
                ///////// FIXME: replace with Val_int(2) for testing (only)
                Val_int(1), /* Marshal.Closures, 2nd constructor */
                ///////// FIXME: replace with Val_int(2) for testing (only)
                caml_pair_r(ctx,
                            Val_int(2), /* Marshal.Cross_context, 3rd constructor */
                            Val_emptylist))
//)
    ;

  /* Marshal the big data structure into a byte array: */
  caml_output_value_to_malloc_r(ctx, globals_and_data, flags,
                                &serialized_tuple, &serialized_tuple_length);
  fprintf(stderr, "Ok-Q 100: ...serialized the huge structure into the blob at %p (length %.2fMB).\n", (void*)globals_and_data, serialized_tuple_length / 1024. / 1024.);

  CAMLreturnT(char*, serialized_tuple);
}

static void caml_install_globals_and_data_as_c_byte_array_r(CAML_R, value *to_values, char *globals_and_data_as_c_array){
  /* No need to GC-protect anything here.  We have no Caml objects to
     GC-protect before initializing globals_and_data by a call to
     caml_input_value_from_malloc_r.  After that call we don't
     allocate anything in this function. */
  value globals_and_data, global_tuple, data_tuple;
  size_t to_value_no __attribute__((unused));
  //fprintf(stderr, "Ok-A 100\n");

  /* Deserialize globals and data from the byte array, and access each
     element of the pair. */
  //fprintf(stderr, "Context %p: L0 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
caml_acquire_global_lock();
  globals_and_data =
    caml_input_value_from_block_r(ctx,
                                  globals_and_data_as_c_array,
                                  /* FIXME: this third parameter is useless in practice: ask the OCaml people to
                                     provide an alternate version of caml_input_value_from_block_r with two parameters.
                                     I don't want to mess up the interface myself, since I'm doing a lot of other
                                     invasive changes --Luca Saiu REENTRANTRUNTIME */
                                  LONG_MAX);
caml_release_global_lock();
  //fprintf(stderr, "Context %p: L1 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
    //caml_input_value_from_malloc_r(ctx, globals_and_data_as_c_array, 0); // this also frees the buffer */
  global_tuple = Field(globals_and_data, 0);
  data_tuple = Field(globals_and_data, 1);
  //fprintf(stderr, "Context %p: L2 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);

  /* Replace the context globals with what we got: */
  caml_set_globals_r(ctx, global_tuple);
  //fprintf(stderr, "Context %p: L3 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);

  /* Copy deserialized data from the tuple where the user requested; the tuple
     will be GC'd: */
  caml_copy_tuple_elements_r(ctx,
                             to_values, &to_value_no,
                             data_tuple);
  //fprintf(stderr, "Context %p: L4 [thread %p]\n", ctx, (void*)(pthread_self())); fflush(stderr);
  //fprintf(stderr, "Ok-A 600 (the tuple has %i elements)\n", (int)to_value_no);
}

/* Implement the interface specified in the header file. */

/* struct caml_context_blob{ */
/*   char *data; */
/*   int reference_count; */
/* }; /\* struct *\/ */

static char* caml_serialize_context(CAML_R, value function)
{
  CAMLparam1(function);
  char *result = caml_globals_and_data_as_c_byte_array_r(ctx, &function, 1);
  CAMLreturnT(char*, result);
}

/* Return 0 on success and non-zero on failure. */
static int caml_run_in_this_thread_r(CAML_R, value function, int index)
{
  // FIXME: move part of caml_deserialize_and_run_in_this_thread here.
}

/* Return 0 on success and non-zero on failure. */
static int caml_deserialize_and_run_in_this_thread(char *blob, int index, sem_t *semaphore, /*out*/caml_global_context **to_context)
{
  int did_we_fail;
  CAML_R = caml_make_empty_context(); // ctx also becomes the thread-local context
  CAMLparam0();
  CAMLlocal2(function, result_or_exception);
  *to_context = ctx;

fprintf(stderr, "======Forcing a GC\n"); fflush(stderr);
caml_gc_compaction_r(ctx, Val_unit); //!!!!!
fprintf(stderr, "======It's ok to have warnings about the lack of globals up to this point\n"); fflush(stderr);

//fprintf(stderr, "W0[context %p] [thread %p] (index %i) BBBBBBBBBBBBBBBBBBBBBBBBBB\n", ctx, (void*)(pthread_self()), index); fflush(stderr); caml_acquire_global_lock(); // FIXME: a test. this is obviously unusable in production
  fprintf(stderr, "W1 [context %p] ctx->caml_local_roots is %p\n", ctx, caml_local_roots); fflush(stderr);
  /* Make a new context, and deserialize the blob into it: */
  fprintf(stderr, "W3 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);

  // Allocate some trash:
  caml_pair_r(ctx,
              caml_pair_r(ctx, Val_int(1), Val_int(2)),
              caml_pair_r(ctx, Val_int(3), Val_int(4)));

  caml_install_globals_and_data_as_c_byte_array_r(ctx, &function, blob);
  fprintf(stderr, "W4 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!

/* caml_empty_minor_heap_r(ctx); */
/* caml_finish_major_cycle_r (ctx); */
/* caml_compact_heap_r (ctx); */
/* caml_final_do_calls_r (ctx); */

  fprintf(stderr, "W5 [context %p] [thread %p] (index %i) (function %p).  About to V the semaphore.\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);

  /* We're done with the blob: unpin it via the semaphore, so that it
     can be destroyed when all threads have deserialized. */
//fprintf(stderr, "W5.5context %p] [thread %p] (index %i) EEEEEEEEEEEEEEEEEEEEEEEEEE\n", ctx, (void*)(pthread_self()), index); fflush(stderr); caml_release_global_lock(); // FIXME: a test. this is obviously inefficient
  sem_post(semaphore);

  /* Run the Caml function: */
  fprintf(stderr, "W6 [context %p] [thread %p] (index %i) (function %p)\n", ctx, (void*)(pthread_self()), index, (void*)function); fflush(stderr);
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  fprintf(stderr, "W7 [context %p] [thread %p] (index %i) (%i globals) ctx->caml_local_roots is %p\n", ctx, (void*)(pthread_self()), index, (int)(ctx->caml_globals.used_size / sizeof(value)), caml_local_roots); fflush(stderr);
  caml_dump_global_mutex();

  /* It's important that Extract_exception be used before the next
     collection, because result_or_exception is an invalid value in
     case of exception: */
  result_or_exception = caml_callback_exn_r(ctx, function, Val_int(index));
//int i;for(i=0;i<3;i++){
  did_we_fail = Is_exception_result(result_or_exception);
  if(did_we_fail){
    /* FIXME: we can't just do "caml_raise_r(ctx, Extract_exception(result_or_exception));".

       If we want to propagate the exception to the parent context we
       have to serialize the exception object, and then deserialize it
       and raise it in the parent context.  Is that useful? */
    result_or_exception = Extract_exception(result_or_exception);
    /* FIXME: shall we do something with the result?  Really?  It's simpler to just discard it. */
  }
//}
  /* Ok, we're done with ctx.  Free its resources, and we're done: */

  /* FIXME: divide this functions into two parts, so that we can call
     caml_destroy_context out of a CAMLparamX...CAMLreturnX block. */
  caml_destroy_context(ctx);

  CAMLreturnT(int, did_we_fail);
}

struct caml_thread_arguments{
  char *blob;
  sem_t *semaphore;
  caml_global_context **split_contexts;
  int index;
}; /* struct */

static void* caml_deserialize_and_run_in_this_thread_as_thread_function(void *args_as_void_star)
{
  struct caml_thread_arguments *args = args_as_void_star;
  //fprintf(stderr, "Q0 (index %i)\n", args->index);
  //sleep(12); // FIXME: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //fprintf(stderr, "Q1 (index %i)\n", args->index);
  int did_we_fail = caml_deserialize_and_run_in_this_thread(args->blob, args->index, args->semaphore, args->split_contexts + args->index);
  fprintf(stderr, "Q2 (index %i) [about to free args].  Did we fail? %i\n", args->index, did_we_fail); fflush(stderr);
  caml_stat_free(args);
  fprintf(stderr, "Q3 (index %i): about to exit the thread\n", args->index); fflush(stderr);
  return (void*)(long)did_we_fail;
}
static void caml_split_and_destroy_blob_r(CAML_R, char *blob, caml_global_context **split_contexts, size_t how_many, sem_t *semaphore)
{
  fprintf(stderr, "CONTEXT %p: >>>> The parent context is %p\n", ctx, ctx);
#ifdef NATIVE_CODE
  fprintf(stderr, "@@@@@ In the parent context caml_bottom_of_stack is %p\n", caml_bottom_of_stack);
#endif // #ifdef NATIVE_CODE
  fprintf(stderr, "CONTEXT %p: >>>> A nice collection before starting...\n", ctx);
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  fprintf(stderr, "CONTEXT %p: >>>> Still alive.  Good.  Now creating threds.\n", ctx);
  int i;
  for(i = 0; i < how_many; i ++){
    //sleep(10); // FIXME: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    pthread_t thread;
    struct caml_thread_arguments *args = caml_stat_alloc(sizeof(struct caml_thread_arguments));
    int pthread_create_result;
    args->blob = blob;
    args->semaphore = semaphore;
    args->split_contexts = split_contexts;
    args->index = i;
    pthread_create_result =
      pthread_create(&thread, NULL, caml_deserialize_and_run_in_this_thread_as_thread_function, args);
    if(pthread_create_result != 0)
      caml_failwith_r(ctx, "pthread_create failed"); // FIXME: blob is leaked is this case
  } /* for */
  /* Wait for the last thread to use the blob, then destroy it: */
  fprintf(stderr, "Context %p: >>>> Waiting for every thread to deserialize...\n", ctx);
  for(i = 0; i < how_many; i ++){
    fprintf(stderr, "Context %p: >>>> Before doing P; showing the mutex\n", ctx); caml_dump_global_mutex();
    sem_wait(semaphore);
    fprintf(stderr, "Context %p: >>>> One child thread has finished with the blob; waiting for %i more...\n", ctx, (int)(how_many - i - 1));
  }
  fprintf(stderr, "Context %p: >>>> All child threads have finished with the blob.  Destroying the blob...\n", ctx);
  caml_stat_free(blob);
  fprintf(stderr, "Context %p: >>>> Done, still alive after free'ing the blob\n", ctx);
  caml_gc_compaction_r(ctx, Val_unit); //!!!!!
  fprintf(stderr, "Context %p: >>>> Done, still alive after a GC\n", ctx);
}

void caml_split_context_r(CAML_R,
                          caml_global_context **split_contexts,
                          value function,
                          size_t how_many)
{
  CAMLparam1(function);
  CAMLlocal1(open_channels);
  sem_t semaphore;
  int init_result = sem_init(&semaphore, /*not process-shared*/0, /*initial value*/0);
  if(init_result != 0)
    caml_failwith_r(ctx, "sem_init failed");

  /* Make sure that the currently-existing channels stay alive until
     after deserialization; we can't keep reference counts within the
     blob, so we pin all alive channels by keeping this list alive: */
  open_channels = caml_ml_all_channels_list_r(ctx);

  /* Serialize the context in the main thread, then create threads,
     and in each one of them deserialize it back in parallel:  */
  char *blob = caml_serialize_context(ctx, function);
  caml_split_and_destroy_blob_r(ctx, blob, split_contexts, how_many, &semaphore);

  sem_destroy(&semaphore);
  fprintf(stderr, "Context %p: ]]]]] Still alive after splitting and destroying the blob.  Good.\n", ctx);
  CAMLreturn0;
}

CAMLprim value caml_context_split_r(CAML_R, value function, value thread_no_as_value)
{
  CAMLparam1(function);
  CAMLlocal1(result);
  int thread_no = Int_val(thread_no_as_value);
  caml_global_context **new_contexts = caml_stat_alloc(sizeof(caml_global_context*) * thread_no);
  caml_split_context_r(ctx, new_contexts, function, thread_no);
  fprintf(stderr, "Context %p: ]]]] Copying the new context (descriptors) into the Caml data structure result\n", ctx);
  result = caml_alloc_r(ctx, thread_no, 0);
  int i;
  for(i = 0; i < thread_no; i ++)
    caml_initialize_r(ctx, &Field(result, i), caml_value_of_context_descriptor(new_contexts[i]->descriptor));
  caml_stat_free(new_contexts);
  fprintf(stderr, "Context %p: ]]]] Destroyed the malloced buffer of pointers new_contexts.  Good.\n", ctx);
  CAMLreturn(result);
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
  Assert(descriptor->kind == caml_global_context_nonmain_local);
  //fprintf(stderr, "!!!! JOINING %p\n", (void*)descriptor->content.local_context.context->thread); fflush(stderr);
  pthread_join_result = pthread_join(descriptor->content.local_context.context->thread, &did_we_fail_as_void_star);
  did_we_fail = (int)(long)did_we_fail_as_void_star;
  fprintf(stderr, "!!!! JOINED %p: did we fail? %i\n", (void*)descriptor->content.local_context.context->thread, did_we_fail); fflush(stderr);
  if(pthread_join_result != 0)
    caml_failwith_r(ctx, "caml_context_join_r: pthread_join failed");
  CAMLreturn(Val_unit);
}
