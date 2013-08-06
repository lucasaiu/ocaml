/* Luca Saiu, REENTRANTRUNTIME */
#ifndef CAML_CONTEXT_SPLIT_H
#define CAML_CONTEXT_SPLIT_H

#include "context.h"

// // FIXME: remove from the header.  This should not be public
// typedef struct caml_context_blob* caml_context_blob_t;

/* Serialize the given Caml object, which must belong to the given
   context, into a malloc'ed buffer. */
char* caml_serialize_into_blob_r(CAML_R, value object);

/* Deserialize a buffer as returned by caml_serialize_r into a Caml
   object for the given context (usually different from the
   serialization context).  This does *not* free the buffer. */
value caml_deserialize_blob_r(CAML_R, char *blob);

/* Split the given context into how_many copies.  Each one is
   associated to a different new thread.  For each thread the given
   int -> unit function with an index from 0 to how_many - 1.  Store
   new context pointers into split_contexts.  When the function returns,
   all the new contexts have been initialized. */
void caml_split_context_r(CAML_R,
                          caml_global_context **split_contexts,
                          value function,
                          size_t how_many);

struct caml_mailbox* caml_make_local_mailbox_r(CAML_R);
void caml_destroy_local_mailbox_r(CAML_R, struct caml_mailbox *mailbox);

/* Run the context finalization functions registered with Context.at_exit */
void caml_run_at_context_exit_functions_r(CAML_R);

#endif /* #ifndef CAML_CONTEXT_SPLIT_H */
