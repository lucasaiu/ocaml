/* Luca Saiu, REENTRANTRUNTIME */
#ifndef CAML_CONTEXT_SPLIT_H
#define CAML_CONTEXT_SPLIT_H

#include "context.h"

/* Clone a given context and the given set of caml values potentially
   referring it; return the new context, and store the cloned values
   potentially referring the new context at the given address.
   Sharing among such values, or between values and global data
   structures, is reproduced. */
caml_global_context* caml_split_context(caml_global_context *ctx,
                                        value *to_values,
                                        value *from_values,
                                        size_t value_no);

#endif /* #ifndef CAML_CONTEXT_SPLIT_H */
