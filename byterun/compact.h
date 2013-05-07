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

#ifndef CAML_COMPACT_H
#define CAML_COMPACT_H


#include "config.h"
#include "misc.h"
#include "context.h" /* for CAML_R */

extern void caml_compact_heap_r (CAML_R);
extern void caml_compact_heap_maybe_r (CAML_R);


#endif /* CAML_COMPACT_H */
