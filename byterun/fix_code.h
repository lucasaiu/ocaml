/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Handling of blocks of bytecode (endianness switch, threading). */

#ifndef CAML_FIX_CODE_H
#define CAML_FIX_CODE_H


#include "config.h"
#include "misc.h"
#include "mlvalues.h"

void caml_init_code_fragments_r(CAML_R);
void caml_load_code_r (CAML_R, int fd, asize_t len);
void caml_fixup_endianness_r (CAML_R, code_t code, asize_t len);
void caml_set_instruction_r (CAML_R, code_t pos, opcode_t instr);
int caml_is_instruction_r (CAML_R, opcode_t instr1, opcode_t instr2);

#ifdef THREADED_CODE
void caml_thread_code_r (CAML_R, code_t code, asize_t len);
#endif

#endif /* CAML_FIX_CODE_H */
