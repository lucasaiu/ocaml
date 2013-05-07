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

/* Trace the instructions executed */

#ifdef DEBUG

#define CAML_CONTEXT_DYNLINK
#define CAML_CONTEXT_STACKS
#define CAML_CONTEXT_FIX_CODE // for caml_start_code
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "instruct.h"
#include "misc.h"
#include "mlvalues.h"
#include "opnames.h"
#include "prims.h"
#include "stacks.h"

//extern code_t caml_start_code; // REENTRANTRUNTIME

intnat caml_icount = 0;

void caml_stop_here () {}

int caml_trace_flag = 0;

void caml_disasm_instr_r(CAML_R, code_t pc)
{
  int instr = *pc;
  printf("%6ld  %s", (long) (pc - caml_start_code),
         instr < 0 || instr > STOP ? "???" : names_of_instructions[instr]);
  pc++;
  switch(instr) {
    /* Instructions with one integer operand */
  case PUSHACC: case ACC: case POP: case ASSIGN:
  case PUSHENVACC: case ENVACC: case PUSH_RETADDR: case APPLY:
  case APPTERM1: case APPTERM2: case APPTERM3: case RETURN:
  case GRAB: case PUSHGETGLOBAL: case GETGLOBAL: case SETGLOBAL:
  case PUSHATOM: case ATOM: case MAKEBLOCK1: case MAKEBLOCK2:
  case MAKEBLOCK3: case MAKEFLOATBLOCK:
  case GETFIELD: case SETFIELD: case GETFLOATFIELD: case SETFLOATFIELD:
  case BRANCH: case BRANCHIF: case BRANCHIFNOT: case PUSHTRAP:
  case CONSTINT: case PUSHCONSTINT: case OFFSETINT: case OFFSETREF:
  case OFFSETCLOSURE: case PUSHOFFSETCLOSURE:
    printf(" %d\n", pc[0]); break;
    /* Instructions with two operands */
  case APPTERM: case CLOSURE: case CLOSUREREC: case PUSHGETGLOBALFIELD:
  case GETGLOBALFIELD: case MAKEBLOCK:
  case BEQ: case BNEQ: case BLTINT: case BLEINT: case BGTINT: case BGEINT:
  case BULTINT: case BUGEINT:
    printf(" %d, %d\n", pc[0], pc[1]); break;
    /* Instructions with a C primitive as operand */
  case C_CALLN:
    printf(" %d,", pc[0]); pc++;
    /* fallthrough */
  case C_CALL1: case C_CALL2: case C_CALL3: case C_CALL4: case C_CALL5:
    if (pc[0] < 0 || pc[0] >= caml_prim_name_table.size)
      printf(" unknown primitive %d\n", pc[0]);
    else
      printf(" %s\n", (char *) caml_prim_name_table.contents[pc[0]]);
    break;
  default:
    printf("\n");
  }
  fflush (stdout);
}

char * caml_instr_string_t (CAML_R, code_t pc)
{
  static char buf[256];
  char nambuf[128];
  int instr = *pc;
  char *nam;

  nam = (instr < 0 || instr > STOP)
    ? (sprintf (nambuf, "???%d", instr), nambuf)
    : names_of_instructions[instr];
  pc++;
  switch (instr) {
    /* Instructions with one integer operand */
  case PUSHACC:
  case ACC:
  case POP:
  case ASSIGN:
  case PUSHENVACC:
  case ENVACC:
  case PUSH_RETADDR:
  case APPLY:
  case APPTERM1:
  case APPTERM2:
  case APPTERM3:
  case RETURN:
  case GRAB:
  case PUSHGETGLOBAL:
  case GETGLOBAL:
  case SETGLOBAL:
  case PUSHATOM:
  case ATOM:
  case MAKEBLOCK1:
  case MAKEBLOCK2:
  case MAKEBLOCK3:
  case MAKEFLOATBLOCK:
  case GETFIELD:
  case SETFIELD:
  case GETFLOATFIELD:
  case SETFLOATFIELD:
  case BRANCH:
  case BRANCHIF:
  case BRANCHIFNOT:
  case PUSHTRAP:
  case CONSTINT:
  case PUSHCONSTINT:
  case OFFSETINT:
  case OFFSETREF:
  case OFFSETCLOSURE:
  case PUSHOFFSETCLOSURE:
    sprintf(buf, "%s %d", nam, pc[0]);
    break;
    /* Instructions with two operands */
  case APPTERM:
  case CLOSURE:
  case CLOSUREREC:
  case PUSHGETGLOBALFIELD:
  case GETGLOBALFIELD:
  case MAKEBLOCK:
  case BEQ:
  case BNEQ:
  case BLTINT:
  case BLEINT:
  case BGTINT:
  case BGEINT:
  case BULTINT:
  case BUGEINT:
    sprintf(buf, "%s %d, %d", nam, pc[0], pc[1]);
    break;
  case SWITCH:
    sprintf(buf, "SWITCH sz%#lx=%ld::ntag%ld nint%ld",
            (long) pc[0], (long) pc[0], (unsigned long) pc[0] >> 16,
            (unsigned long) pc[0] & 0xffff);
    break;
    /* Instructions with a C primitive as operand */
  case C_CALLN:
    sprintf(buf, "%s %d,", nam, pc[0]);
    pc++;
    /* fallthrough */
  case C_CALL1:
  case C_CALL2:
  case C_CALL3:
  case C_CALL4:
  case C_CALL5:
    if (pc[0] < 0 || pc[0] >= caml_prim_name_table.size)
      sprintf(buf, "%s unknown primitive %d", nam, pc[0]);
    else
      sprintf(buf, "%s %s", nam, (char *) caml_prim_name_table.contents[pc[0]]);
    break;
  default:
    sprintf(buf, "%s", nam);
    break;
  };
  return buf;
}


void
caml_trace_value_file_r (CAML_R, value v, code_t prog, int proglen, FILE * f)
{
  int i;
  fprintf (f, "%#lx", v);
  if (!v)
    return;
  if (prog && v % sizeof (int) == 0
           && (code_t) v >= prog
           && (code_t) v < (code_t) ((char *) prog + proglen))
    fprintf (f, "=code@%d", (int)((code_t) v - prog));
  else if (Is_long (v))
    fprintf (f, "=long%" ARCH_INTNAT_PRINTF_FORMAT "d", Long_val (v));
  else if ((void*)v >= (void*)caml_stack_low
           && (void*)v < (void*)caml_stack_high)
    fprintf (f, "=stack_%d", (int)((intnat*)caml_stack_high - (intnat*)v));
  else if (Is_block (v)) {
    int s = Wosize_val (v);
    int tg = Tag_val (v);
    int l = 0;
    switch (tg) {
    case Closure_tag:
      fprintf (f, "=closure[s%d,cod%d]", s, (int)((code_t) (Code_val (v)) - prog));
      goto displayfields;
    case String_tag:
      l = caml_string_length (v);
      fprintf (f, "=string[s%dL%d]'", s, l);
      for (i = 0; i < ((l>0x1f)?0x1f:l) ; i++) {
        if (isprint (Byte (v, i)))
          putc (Byte (v, i), f);
        else
          putc ('?', f);
      };
      fprintf (f, "'");
      goto displayfields;
    case Double_tag:
      fprintf (f, "=float[s%d]=%g", s, Double_val (v));
      goto displayfields;
    case Double_array_tag:
      fprintf (f, "=floatarray[s%d]", s);
      for (i = 0; i < ((s>0xf)?0xf:s); i++)
        fprintf (f, " %g", Double_field (v, i));
      goto displayfields;
    case Abstract_tag:
      fprintf (f, "=abstract[s%d]", s);
      goto displayfields;
    case Custom_tag:
      fprintf (f, "=custom[s%d]", s);
      goto displayfields;
    default:
      fprintf (f, "=block<T%d/s%d>", tg, s);
    displayfields:
      if (s > 0)
        fputs ("=(", f);
      for (i = 0; i < s; i++) {
        if (i > 20) {
          fputs ("....", f);
          break;
        };
        if (i > 0)
          putc (' ', f);
        fprintf (f, "%#lx", Field (v, i));
      };
      if (s > 0)
        putc (')', f);
    };
  }
}

void
caml_trace_accu_sp_file_r (CAML_R, value accu, value * sp, code_t prog, int proglen,
                         FILE * f)
{
  int i;
  value *p;
  fprintf (f, "accu=");
  caml_trace_value_file_r (ctx, accu, prog, proglen, f);
  fprintf (f, "\n sp=%#" ARCH_INTNAT_PRINTF_FORMAT "x @%d:",
           (intnat) sp, (int)(caml_stack_high - sp));
  for (p = sp, i = 0; i < 12 + (1 << caml_trace_flag) && p < caml_stack_high;
       p++, i++) {
    fprintf (f, "\n[%d] ", (int)(caml_stack_high - p));
    caml_trace_value_file_r (ctx, *p, prog, proglen, f);
  };
  putc ('\n', f);
  fflush (f);
}

#endif /* DEBUG */
