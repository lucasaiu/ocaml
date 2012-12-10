/* Luca Saiu, REENTRANTRUNTIME */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define CAML_CONTEXT_ROOTS /* GC-protection macros */
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

#include "gc_ctrl.h" // FIXME: remove after debugging
#include "compact.h" // FIXME: remove after debugging

static value caml_tuple_of_c_array_r(CAML_R, value *array, size_t element_no)
{
  /* No need for GC protection: this is the only allocation, and if
     the GC moves the objects pointed by array at allocation time,
     that's no problem. */
  //printf(">>>>>>>>>element_no is %i\n", (int)element_no);
  value result = caml_alloc_tuple_r(ctx, element_no);
  int i;
  for(i = 0; i < element_no; i ++)
    caml_initialize_r(ctx, &Field(result, i), array[i]);
  return result;
}

static void caml_copy_tuple_elements_r(CAML_R, value *to_array, size_t *to_element_no, value from_tuple)
{
  /* No need for GC-protection: there is no allocation here. */
  size_t element_no = Wosize_val(from_tuple);
  *to_element_no = element_no;
  int i;
  for(i = 0; i < element_no; i ++)
    to_array[i] = Field(from_tuple, i);
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
  /* This is the only allocation: no need fot GC protection: */
  value globals = caml_alloc_tuple_r(ctx, global_no);
  int i;
  for(i = 0; i < global_no; i ++)
    caml_initialize_r(ctx, &Field(globals, i), ((value*)ctx->caml_globals.array)[i]);

  int element_no = Wosize_val(globals);
  printf("[native] The tuple has %i elements; it should be %i\n", (int)element_no, (int)global_no);

  return globals;
#else /* bytecode */
  /* No need for GC-protection: there is no allocation here. */
  // FIXME: for debugging only.  Remove: BEGIN
  value globals = ctx->caml_global_data;
  int element_no = Wosize_val(globals);
  printf("[bytecode] The tuple has %i elements\n", (int)element_no);
  // FIXME: for debugging only.  Remove: END

  return ctx->caml_global_data;
#endif /* #else, #ifdef NATIVE_CODE */
}

CAMLprim value caml_global_array_r(CAML_R, value unit)
{
  return caml_global_tuple_r(ctx);

  /* CAMLparam0(); */
  /* CAMLlocal2(result_as_tuple, result); */
  /* int i; */
  /* size_t element_no; */
  /* result_as_tuple = caml_global_tuple_r(ctx); */
  /* element_no = Wosize_val(result_as_tuple); */
  /* result = caml_alloc_r(ctx, element_no, 0); */
  /* for(i = 0; i < element_no; i ++) */
  /*   caml_initialize_r(ctx, &Field(result, i), Field(result_as_tuple, i)); */
  /* CAMLreturn(result); */
}

/* Replace the globals of the given context with the elements of the given tuple: */
void caml_set_globals_r(CAML_R, value global_tuple){
  /* No need to GC-protect anything here: we do not allocate anything
     from the Caml heap, in either branch. */
#ifdef NATIVE_CODE
  size_t global_tuple_size = Wosize_val(global_tuple);
  printf("SETGLOBALS: there are %i globals\n", (int)global_tuple_size);
  caml_resize_extensible_buffer(&ctx->caml_globals,
                                global_tuple_size * sizeof(value),
                                1);
  void* to_globals = ctx->caml_globals.array;
  size_t to_global_no;
  caml_copy_tuple_elements_r(ctx,
                             to_globals, &to_global_no,
                             global_tuple);
  Assert(to_global_no == global_tuple_size);
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
  printf("Ok-Q 100: ...serialized the huge structure at %p (length %.2fMB).  Still alive.  Good.\n", (void*)globals_and_data, serialized_tuple_length / 1024. / 1024.);

  CAMLreturnT(char*, serialized_tuple);
}

/* This function also frees the buffer pointed by globals_and_data_as_c_array, which must be malloc'ed. */
static void caml_install_globals_and_data_as_c_byte_array_r(CAML_R, value *to_values, char *globals_and_data_as_c_array){
  /* No need to GC-protect anything here.  We have no Caml objects to
     GC-protect before initializing globals_and_data by a call to
     caml_input_value_from_malloc_r.  After that call we don't
     allocate anything in this function. */
  value globals_and_data, global_tuple, data_tuple;
  size_t to_value_no __attribute__((unused));
  printf("Ok-A 100\n");

  /* Deserialize globals and data from the byte array, and access each
     element of the pair. */
  globals_and_data =
    caml_input_value_from_malloc_r(ctx, globals_and_data_as_c_array, 0); // this also frees the buffer */
  global_tuple = Field(globals_and_data, 0);
  data_tuple = Field(globals_and_data, 1);

  /* Replace the context globals with what we got: */
  caml_set_globals_r(ctx, global_tuple);
  printf("Ok-A 500\n");

  /* Copy deserialized data from the tuple where the user requested; the tuple
     will be GC'd: */
  caml_copy_tuple_elements_r(ctx,
                             to_values, &to_value_no,
                             data_tuple);
  printf("Ok-A 600\n");
}

/* No "_r" suffix, nor CAML_R here: I view this function as quite
   special since its the first parameter is not "the implicit current
   context". */
caml_global_context* caml_split_context(caml_global_context *from_ctx,
                                        value *to_values,
                                        value *from_values,
                                        size_t value_no){
  char *serialized_data =
    caml_globals_and_data_as_c_byte_array_r(from_ctx, from_values, value_no);

  caml_global_context *to_ctx = caml_make_empty_context();
  caml_set_global_context(to_ctx); // FIXME: this is horrible
  caml_install_globals_and_data_as_c_byte_array_r(to_ctx, to_values, serialized_data);
  caml_set_global_context(from_ctx); // FIXME: this is horrible
  return to_ctx;
}

static void caml_run_in_new_context(CAML_R, char *serialized_data){
  CAMLparam0();
  CAMLlocal1(new_thunk);
  caml_set_global_context(ctx); // FIXME: this is horrible
  //caml_local_roots = NULL;
  caml_install_globals_and_data_as_c_byte_array_r(ctx, &new_thunk, serialized_data); // this free's the C buffer
  caml_callback_r(ctx, new_thunk, Val_unit);
  fprintf(stderr, "caml_run_in_new_context: SUCCESS, about to exit\n");
  CAMLreturn0;
}

/* A function with an interface easier to call from OCaml: */
CAMLprim value caml_context_fork_and_run_thunk_r(CAML_R, value thunk){
  CAMLparam1(thunk);
  printf("OK-PP(a): %p\n", (void*)thunk);
  //caml_gc_minor_r(ctx, Val_unit);
  //int i; for(i = 0; i < 10; i++) caml_gc_minor_r(ctx, Val_unit);
  caml_gc_compaction_r(ctx, Val_unit);
  printf("OK-PP(b): %p\n", (void*)thunk);
  //CAMLlocal1(new_thunk);

  char *serialized_data =
    caml_globals_and_data_as_c_byte_array_r(ctx, &thunk, 1);

  printf("OK-PP(c): %p\n", (void*)thunk);
  int fork_result = fork();
  switch(fork_result){
  case -1:
    caml_failwith_r(ctx, "fork failed");
// FIXME: reverse the child and parent branches
  //default: /* parent process */
  case 0:
    fprintf(stderr, "[Hello from the child process]\n");
    free(serialized_data);
    break;
  //case 0: /* child process */
  default:
    fprintf(stderr, "[Hello from the parent process]\n");
    // FIXME: handle exceptions

    caml_global_context *new_context = caml_make_empty_context();
    caml_run_in_new_context(new_context, serialized_data);
    exit(EXIT_SUCCESS);
    //CAMLreturn(Val_unit); // unreachable
  } /* switch */
  //CAMLreturn(caml_value_of_context_descriptor(new_context->descriptor));
  CAMLreturn(Val_unit); // unreachable
}

/* /\* A function with an interface easier to call from OCaml: *\/ */
/* CAMLp___rim value caml_context_fork_and_run_thunk_r(CAML_R, value thunk){ */
/*   CAMLparam1(thunk); */
/*   printf("OK-PP(a): %p\n", (void*)thunk); */
/*   //caml_gc_minor_r(ctx, Val_unit); */
/*   //int i; for(i = 0; i < 10; i++) caml_gc_minor_r(ctx, Val_unit); */
/*   caml_gc_compaction_r(ctx, Val_unit); */
/*   printf("OK-PP(b): %p\n", (void*)thunk); */
/*   //CAMLlocal1(new_thunk); */
/*   value new_thunk; // It's important *NOT* to GC-protect this: it belongs to the new context!! */
/*   caml_global_context *new_context = caml_split_context(ctx, &new_thunk, &thunk, 1); */

/*   /// KLUDGE: BEGIN */
/*   printf("OK-QQ(b): %p\n", (void*)new_thunk); */
/*   //caml_oldify_one_r (new_context, new_thunk, &new_thunk); */
/*   printf("OK-QQ(b): %p\n", (void*)new_thunk); */
/*   //caml_oldify_mopup_r (new_context); */
/*   printf("OK-QQ(b): %p\n", (void*)new_thunk); */
/*   caml_gc_compaction_r(new_context, Val_unit); */
/*   printf("OK-QQ(b): %p\n", (void*)new_thunk); */
/*   /// KLUDGE: END */

/*   printf("OK-PP(c): %p\n", (void*)thunk); */
/*   int fork_result = fork(); */
/*   switch(fork_result){ */
/*   case -1: */
/*     caml_failwith_r(ctx, "fork failed"); */
/*   // FIXME: swap the 0: and default: branches again after debugging */
/*   case 0: /\* child process *\/ */
/*     fprintf(stderr, "[Hello from the child process]\n"); */
/*     // FIXME: handle exceptions */
/*     caml_callback_r(new_context, new_thunk, Val_unit); */
/*     fprintf(stderr, "[The child process is about to exit]\n"); */
/*     exit(EXIT_SUCCESS); */
/*   default: /\* parent process *\/ */
/*     fprintf(stderr, "[Hello from the parent process]\n"); */
/*     break; */
/*   } /\* switch *\/ */
/*   CAMLreturn(caml_value_of_context_descriptor(new_context->descriptor)); */
/* } */

/* A function with an interface easier to call from OCaml: */
struct caml_thread_args{
  caml_global_context *context;
  value thunk;
};
static void caml_thread_routine_with_reasonable_types_r(CAML_R, value thunk){
  CAMLparam1(thunk);
  fprintf(stderr, "[From the child thread: running the OCaml code]\n");
  // FIXME: handle exceptions
  caml_callback_r(ctx, thunk, Val_unit);
  fprintf(stderr, "[The child thread is about to exit]\n");
  
  CAMLreturn0;
}

static void* caml_thread_routine(void *arg_as_void_star){
  struct caml_thread_args *args = arg_as_void_star;
  fprintf(stderr, "[Hello from the child thread]\n");
  caml_thread_routine_with_reasonable_types_r(args->context, args->thunk);
  free(args);
  fprintf(stderr, "[The child thread is about to exit]\n");
  return NULL;
}
CAMLprim value caml_context_pthread_create_and_run_thunk_r(CAML_R, value thunk){
  CAMLparam1(thunk);
  CAMLlocal1(new_thunk);
  caml_global_context *new_context = caml_split_context(ctx, &new_thunk, &thunk, 1);
  pthread_t thread;
  struct caml_thread_args *thread_args = malloc(sizeof(struct caml_thread_args));
  thread_args->context = new_context;
  thread_args->thunk = new_thunk;
  int pthread_create_result = pthread_create(&thread, NULL, caml_thread_routine, thread_args);
  if(pthread_create_result != 0)
    caml_failwith_r(ctx, "pthread_create failed");

  fprintf(stderr, "[Hello from the parent thread]\n");
  CAMLreturn(caml_value_of_context_descriptor(new_context->descriptor));
}

CAMLprim value caml_context_exit_r(CAML_R, value unit){
  printf("The context %p should now exit\n", ctx);
  while(1)
    sleep(10);
  return Val_unit; /* unreachable */
}
