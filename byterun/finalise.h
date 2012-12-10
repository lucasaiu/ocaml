/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*          Damien Doligez, projet Moscova, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2000 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#ifndef CAML_FINALISE_H
#define CAML_FINALISE_H

#include "roots.h"

void caml_final_update_r (CAML_R);
void caml_final_do_calls_r (CAML_R);
void caml_final_do_strong_roots_r (CAML_R, scanning_action f);
void caml_final_do_weak_roots_r (CAML_R, scanning_action f);
void caml_final_do_young_roots_r (CAML_R, scanning_action f);
void caml_final_empty_young_r (CAML_R);
value caml_final_register_r (CAML_R, value f, value v);

#endif /* CAML_FINALISE_H */
