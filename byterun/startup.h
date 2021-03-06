/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2001 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#ifndef CAML_STARTUP_H
#define CAML_STARTUP_H

#include "mlvalues.h"
#include "context.h"
#include "exec.h"

/* Return a new empty context, and make it "the" thread-local current context.
   Its descriptor says that its kins is non-main local, but that can be changed. */
caml_global_context* caml_make_empty_context(void);

/* CAMLextern void caml_main(char **argv); */
CAMLextern caml_global_context * caml_main_rr (char **);

CAMLextern void caml_startup_code( // this creates the first context --Luca Saiu, REENTRANTRUNTIME
           code_t code, asize_t code_size,
           char *data, asize_t data_size,
           char *section_table, asize_t section_table_size,
           char **argv);

enum { FILE_NOT_FOUND = -1, BAD_BYTECODE  = -2 };

extern int caml_attempt_open(char **name, struct exec_trailer *trail,
                             int do_open_script);
extern void caml_read_section_descriptors_r(CAML_R, int fd, struct exec_trailer *trail);
extern int32 caml_seek_optional_section(int fd, struct exec_trailer *trail,
                                        char *name);
extern int32 caml_seek_section(int fd, struct exec_trailer *trail, char *name);


#endif /* CAML_STARTUP_H */
