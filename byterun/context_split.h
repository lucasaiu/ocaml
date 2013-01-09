/* Luca Saiu, REENTRANTRUNTIME */
#ifndef CAML_CONTEXT_SPLIT_H
#define CAML_CONTEXT_SPLIT_H

#include "context.h"

/* // FIXME: remove this */
/* /\* Clone a given context and the given set of caml values potentially */
/*    referring it; return the new context, and store the cloned values */
/*    potentially referring the new context at the given address. */
/*    Sharing among such values, or between values and global data */
/*    structures, is reproduced. *\/ */
/* caml_global_context* caml_split_context(caml_global_context *ctx, */
/*                                         value *to_values, */
/*                                         value *from_values, */
/*                                         size_t value_no); */
/* // FIXME: remove this */

// FIXME: remove from the header.  This should not be public
typedef struct caml_context_blob* caml_context_blob_t;

// FIXME:
/* Split the given context into how_many copies.  Each one is
   associated to a different new thread.  For each thread the given
   int -> unit function with an index from 0 to how_many - 1.  Store
   new context pointers into split_contexts.  When the function returns,
   all the new contexts have been initialized. */
void caml_split_context_r(CAML_R,
                          caml_global_context **split_contexts,
                          value function,
                          size_t how_many);

/* // FIXME: remove from the header.  This should not be public */
/* /\* Given a context and an int -> unit function (from that context) to */
/*    be executed in a new context split from the given one, return a */
/*    serialized blob.  The returned blob is allocated with malloc, and */
/*    can be destroyed with free. *\/ */
/* char* caml_serialize_context_r(CAML_R, value function); */

/* // FIXME: remove from the header.  This should not be public */
/* /\* Given a serialized blob, split its context content into the given */
/*    number of contexts, each in a distinct new thread.  Run the */
/*    function from the blob in each context with a zero-based index as a */
/*    parameter.  Store pointers to the the created contexts where */
/*    requested.  De-allocate the blob on return. *\/ */
/* void caml_split_serialized_context_r(CAML_R, */
/*                                      char *blob, */
/*                                      caml_global_context **split_contexts, */
/*                                      size_t how_many); */

/* // FIXME: remove from the header.  This should not be public */
/* /\* Deserialize the given blob in the current thread, obtaining a */
/*    context and an int->unit function in that context.  Call the */
/*    function with the given index. *\/ */
/* void caml_deserialize_and_run_in_this_thread(char *blob, int index); */

#endif /* #ifndef CAML_CONTEXT_SPLIT_H */
