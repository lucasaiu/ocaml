/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*             Damien Doligez, projet Para, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Free lists of heap blocks. */

#ifndef CAML_FREELIST_H
#define CAML_FREELIST_H


#include "misc.h"
#include "mlvalues.h"
#include "context.h"

char *caml_fl_allocate_r (CAML_R, mlsize_t);
void caml_fl_init_merge_r (CAML_R);
void caml_fl_reset_r (CAML_R);
char *caml_fl_merge_block_r (CAML_R, char *);
void caml_fl_add_blocks_r (CAML_R, char *);
void caml_make_free_blocks_r (CAML_R, value *, mlsize_t, int, int);
void caml_set_allocation_policy_r (CAML_R, uintnat);


#endif /* CAML_FREELIST_H */
