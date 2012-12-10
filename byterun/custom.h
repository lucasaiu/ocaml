/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Manuel Serrano and Xavier Leroy, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2000 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#ifndef CAML_CUSTOM_H
#define CAML_CUSTOM_H


#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "mlvalues.h"

/* We have to keep this "non-_r" version of the structure for
   compatibility with external libraries.  REENTRANTRUNTIME */
struct custom_operations {
  char *identifier;
  void (*finalize)(value v);
  int (*compare)(value v1, value v2);
  intnat (*hash)(value v);
  void (*serialize)(value v,
                    /*out*/ uintnat * wsize_32 /*size in bytes*/,
                    /*out*/ uintnat * wsize_64 /*size in bytes*/);
  uintnat (*deserialize)(void * dst);
  int (*compare_ext)(value v1, value v2);
  /* ------------------------------------------------------ */
  /* Here come our new fields for multiruntime.  It's particularly
     important not to change or reorder the fields coming *before*
     this point, for compatibility with older pre-multiruntime
     versions.  --Luca Saiu REENTRANTRUNTIME */
  void (*cross_context_serialize)(value v,
                                  /*out*/ uintnat * wsize_32 /*size in bytes*/,
                                  /*out*/ uintnat * wsize_64 /*size in bytes*/);
  uintnat (*cross_context_deserialize)(void * dst); // FIXME: I probably don't want this
};

/* /\* A variant of struct custom_operations holding "_r" functions: REENTRANTRUNTIME*\/ */
/* struct custom_operations_r { */
/*   char *identifier; */
/*   void (*finalize)(CAML_R, value v); */
/*   int (*compare)(CAML_R, value v1, value v2); */
/*   intnat (*hash)(CAML_R, value v); */
/*   void (*serialize)(CAML_R, value v, */
/*                     /\*out*\/ uintnat * wsize_32 /\*size in bytes*\/, */
/*                     /\*out*\/ uintnat * wsize_64 /\*size in bytes*\/); */
/*   uintnat (*deserialize)(CAML_R, void * dst); */
/*   int (*compare_ext)(CAML_R, value v1, value v2); */
/* }; */

#define custom_finalize_default NULL
#define custom_compare_default NULL
#define custom_hash_default NULL
#define custom_serialize_default NULL
#define custom_deserialize_default NULL
#define custom_compare_ext_default NULL

#define Custom_ops_val(v) (*((struct custom_operations **) (v)))

#ifdef __cplusplus
extern "C" {
#endif

/* FIXME: I *suppose* this has to be kept as it is for compatibility: --Luca Saiu REENTRANTRUNTIME */
CAMLextern value caml_alloc_custom(struct custom_operations * ops,
                                   uintnat size, /*size in bytes*/
                                   mlsize_t mem, /*resources consumed*/
                                   mlsize_t max  /*max resources*/);

/* CAMLextern value caml_alloc_custom_r(CAML_R, struct custom_operations_r * ops, */
/*                                      uintnat size, /\*size in bytes*\/ */
/*                                      mlsize_t mem, /\*resources consumed*\/ */
/*                                      mlsize_t max  /\*max resources*\/); */

CAMLextern void caml_register_custom_operations(struct custom_operations * ops);
/* CAMLextern void caml_register_custom_operations_r(struct custom_operations_r * ops); */

/* CAMLextern int caml_compare_unordered; */
  /* Used by custom comparison to report unordered NaN-like cases. */

/* <private> */
extern struct custom_operations * caml_find_custom_operations(char * ident);

/* Return the struct custom_operations pointer associated with the
   given finalizer; if no such struct custom_operations exists make
   one and return its pointer.  --Luca Saiu, REENTRANTRUNTIME; I added
   this comment since I found the purpose of this hard to undertand
   without looking at the source.  This has only one call site, in
   byterun/alloc.c. */
extern struct custom_operations *
          caml_final_custom_operations(void (*fn)(value));

extern void caml_init_custom_operations(void);
/* </private> */

#ifdef __cplusplus
}
#endif

#endif /* CAML_CUSTOM_H */
