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

#ifndef CAML_ALLOC_H
#define CAML_ALLOC_H


#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "misc.h"
#include "mlvalues.h"
#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

CAMLextern value caml_alloc_r (CAML_R, mlsize_t, tag_t);
CAMLextern value caml_alloc_small_r (CAML_R, mlsize_t, tag_t);
CAMLextern value caml_alloc_tuple_r (CAML_R, mlsize_t);
CAMLextern value caml_alloc_string_r (CAML_R, mlsize_t);  /* size in bytes */
CAMLextern value caml_copy_string_r (CAML_R, char const *);
CAMLextern value caml_copy_string_array_r (CAML_R, char const **);
CAMLextern value caml_copy_double_r (CAML_R, double);
CAMLextern value caml_copy_int32_r (CAML_R, int32);       /* defined in [ints.c] */
CAMLextern value caml_copy_int64_r (CAML_R, int64);       /* defined in [ints.c] */
CAMLextern value caml_copy_nativeint_r (CAML_R, intnat);  /* defined in [ints.c] */
CAMLextern value caml_alloc_array_r (CAML_R, value (*funct) (CAML_R, char const *),
                                   char const ** array);

typedef void (*final_fun)(value);
CAMLextern value caml_alloc_final_r (CAML_R, mlsize_t, /*size in words*/
                                   final_fun, /*finalization function*/
                                   mlsize_t, /*resources consumed*/
                                   mlsize_t  /*max resources*/);

CAMLextern int caml_convert_flag_list(value, int *);

#ifdef __cplusplus
}
#endif

#endif /* CAML_ALLOC_H */
