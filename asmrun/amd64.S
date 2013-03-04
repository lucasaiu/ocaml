/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 2003 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* Asm part of the runtime system, AMD64 processor */
/* Must be preprocessed by cpp */

/* PIC mode support based on contribution by Paul Stravers (see PR#4795) */

#include "../config/m.h"

/* The following indices must respect the order of the caml_global_context
   fields in byterun/context.h; they are in fact offsets in words. */
#define caml_young_limit          0
#define caml_young_ptr            1
#define caml_last_return_address  2
#define caml_bottom_of_stack      3
#define caml_gc_regs              4
#define caml_exception_pointer    5
#define caml_backtrace_active     6

#if defined(SYS_macosx)

#define LBL(x) L##x
#define G(r) _##r
#define GREL(r) _##r@GOTPCREL
#define GCALL(r) _##r
#define FUNCTION_ALIGN 2
#define EIGHT_ALIGN 3
#define SIXTEEN_ALIGN 4
#define FUNCTION(name) \
        .globl name; \
        .align FUNCTION_ALIGN; \
        name:

#elif defined(SYS_mingw64)

#define LBL(x) .L##x
#define G(r) r
#undef  GREL
#define GCALL(r) r
#define FUNCTION_ALIGN 4
#define EIGHT_ALIGN 8
#define SIXTEEN_ALIGN 16
#define FUNCTION(name) \
        .globl name; \
        .align FUNCTION_ALIGN; \
        name:

#else

#define LBL(x) .L##x
#define G(r) r
#define GREL(r) r@GOTPCREL
#define GCALL(r) r@PLT
#define FUNCTION_ALIGN 4
#define EIGHT_ALIGN 8
#define SIXTEEN_ALIGN 16
#define FUNCTION(name) \
        .globl name; \
        .type name,@function; \
        .align FUNCTION_ALIGN; \
        name:

#endif

#ifdef ASM_CFI_SUPPORTED
#define CFI_STARTPROC .cfi_startproc
#define CFI_ENDPROC .cfi_endproc
#define CFI_ADJUST(n) .cfi_adjust_cfa_offset n
#else
#define CFI_STARTPROC
#define CFI_ENDPROC
#define CFI_ADJUST(n)
#endif

#if 0 /* REENTRANTRUNTIME: beginning of the disabled part */

#if defined(__PIC__) && !defined(SYS_mingw64)

/* Position-independent operations on global variables. */

/* Store [srcreg] in global [dstlabel].  Clobbers %r11. */
#define STORE_VAR(srcreg,dstlabel) \
        movq    GREL(dstlabel)(%rip), %r11 ; \
        movq    srcreg, (%r11)

/* Load global [srclabel] in register [dstreg].  Clobbers %r11. */
#define LOAD_VAR(srclabel,dstreg) \
        movq    GREL(srclabel)(%rip), %r11 ; \
        movq    (%r11), dstreg

/* Compare global [label] with register [reg].  Clobbers %rax. */
#define CMP_VAR(label,reg) \
        movq    GREL(label)(%rip), %rax ; \
        cmpq    (%rax), reg

/* Test 32-bit global [label] against mask [imm].  Clobbers %r11. */
#define TESTL_VAR(imm,label) \
        movq    GREL(label)(%rip), %r11 ; \
        testl   imm, (%r11)

/* Push global [label] on stack.  Clobbers %r11. */
#define PUSH_VAR(srclabel) \
        movq    GREL(srclabel)(%rip), %r11 ; \
        pushq   (%r11); CFI_ADJUST (8)

/* Pop global [label] off stack.  Clobbers %r11. */
#define POP_VAR(dstlabel) \
        movq    GREL(dstlabel)(%rip), %r11 ; \
        popq    (%r11);  CFI_ADJUST (-8)

/* Record lowest stack address and return address.  Clobbers %rax. */
#define RECORD_STACK_FRAME(OFFSET) \
        pushq   %r11 ; CFI_ADJUST(8); \
        movq    8+OFFSET(%rsp), %rax ; \
        STORE_VAR(%rax,caml_last_return_address) ; \
        leaq    16+OFFSET(%rsp), %rax ; \
        STORE_VAR(%rax,caml_bottom_of_stack) ; \
        popq    %r11; CFI_ADJUST(-8)

#else /* #if defined(__PIC__) && !defined(SYS_mingw64) */

/* Non-PIC operations on global variables.  Slightly faster. */

#define STORE_VAR(srcreg,dstlabel) \
        movq    srcreg, G(dstlabel)(%rip) /* [All of this is disabled]  This is will fail in REENTRANTRUNTIME */

#define LOAD_VAR(srclabel,dstreg) \
        movq    G(srclabel)(%rip), dstreg

#define CMP_VAR(label,reg) \
        cmpq    G(label)(%rip), %r15

#define TESTL_VAR(imm,label) \
        testl   imm, G(label)(%rip)

#define PUSH_VAR(srclabel) \
        pushq   G(srclabel)(%rip) ; CFI_ADJUST(8)

#define POP_VAR(dstlabel) \
        popq    G(dstlabel)(%rip); CFI_ADJUST(-8)

#define RECORD_STACK_FRAME(OFFSET) \
        movq    OFFSET(%rsp), %rax ; \
        STORE_VAR(%rax,caml_last_return_address) ; \
        leaq    8+OFFSET(%rsp), %rax ; \
        STORE_VAR(%rax,caml_bottom_of_stack)

#endif /* #if defined(__PIC__) && !defined(SYS_mingw64) */

#else /* REENTRANTRUNTIME: here comes the new part using context structs */

/* Store [srcreg] in global [dstlabel]. */
#define STORE_VAR(srcreg,dstlabel) \
        movq    srcreg, dstlabel*8(%r13)

/* Load global [srclabel] in register [dstreg]. */
#define LOAD_VAR(srclabel,dstreg) \
        movq    srclabel*8(%r13), dstreg

/* Compare global [label] with register [reg]. */
#define CMP_VAR(label,reg) \
        cmpq    label*8(%r13), reg

/* Test 32-bit global [label] against mask [imm]. */
#define TESTL_VAR(imm,label) \
        testl   imm, label*8(%r13)

/* Push global [label] on stack. */
#define PUSH_VAR(srclabel) \
        pushq    srclabel*8(%r13)

/* Pop global [label] off stack. */
#define POP_VAR(dstlabel) \
        popq	dstlabel*8(%r13)  # FIXED!!!

/* Record lowest stack address and return address. */
#define RECORD_STACK_FRAME(OFFSET) \
        movq    OFFSET(%rsp), %rax ; \
	STORE_VAR(%rax,caml_last_return_address) ; \
        leaq    8+OFFSET(%rsp), %rax ; \
	STORE_VAR(%rax,caml_bottom_of_stack)
//#endif // REENTRANTRUNTIME: FIXME: I think I introduced this #endif by mistake.  FIXME: Verify.

#endif /* REENTRANTRUNTIME: end of our big conditional disbling another conditional */

/* Save and restore all callee-save registers on stack.
   Keep the stack 16-aligned. */

#if defined(SYS_mingw64)

/* Win64 API: callee-save regs are rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15 */

#define PUSH_CALLEE_SAVE_REGS \
        pushq   %rbx; CFI_ADJUST (8); \
        pushq   %rbp; CFI_ADJUST (8); \
        pushq   %rsi; CFI_ADJUST (8); \
        pushq   %rdi; CFI_ADJUST (8); \
        pushq   %r12; CFI_ADJUST (8); \
        pushq   %r13; CFI_ADJUST (8); \
        pushq   %r14; CFI_ADJUST (8); \
        pushq   %r15; CFI_ADJUST (8); \
        subq    $(8+10*16), %rsp; CFI_ADJUST (8+10*16); \
        movupd  %xmm6, 0*16(%rsp); \
        movupd  %xmm7, 1*16(%rsp); \
        movupd  %xmm8, 2*16(%rsp); \
        movupd  %xmm9, 3*16(%rsp); \
        movupd  %xmm10, 4*16(%rsp); \
        movupd  %xmm11, 5*16(%rsp); \
        movupd  %xmm12, 6*16(%rsp); \
        movupd  %xmm13, 7*16(%rsp); \
        movupd  %xmm14, 8*16(%rsp); \
        movupd  %xmm15, 9*16(%rsp)

#define POP_CALLEE_SAVE_REGS \
        movupd  0*16(%rsp), %xmm6; \
        movupd  1*16(%rsp), %xmm7; \
        movupd  2*16(%rsp), %xmm8; \
        movupd  3*16(%rsp), %xmm9; \
        movupd  4*16(%rsp), %xmm10; \
        movupd  5*16(%rsp), %xmm11; \
        movupd  6*16(%rsp), %xmm12; \
        movupd  7*16(%rsp), %xmm13; \
        movupd  8*16(%rsp), %xmm14; \
        movupd  9*16(%rsp), %xmm15; \
        addq    $(8+10*16), %rsp; CFI_ADJUST (-8-10*16); \
        popq    %r15; CFI_ADJUST(-8); \
        popq    %r14; CFI_ADJUST(-8); \
        popq    %r13; CFI_ADJUST(-8); \
        popq    %r12; CFI_ADJUST(-8); \
        popq    %rdi; CFI_ADJUST(-8); \
        popq    %rsi; CFI_ADJUST(-8); \
        popq    %rbp; CFI_ADJUST(-8); \
        popq    %rbx; CFI_ADJUST(-8)

#else

/* Unix API: callee-save regs are rbx, rbp, r12-r15 */

#define PUSH_CALLEE_SAVE_REGS \
        pushq   %rbx; CFI_ADJUST(8); \
        pushq   %rbp; CFI_ADJUST(8); \
        pushq   %r12; CFI_ADJUST(8); \
        pushq   %r13; CFI_ADJUST(8); \
        pushq   %r14; CFI_ADJUST(8); \
        pushq   %r15; CFI_ADJUST(8); \
        subq    $8, %rsp; CFI_ADJUST(8)

#define POP_CALLEE_SAVE_REGS \
        addq    $8, %rsp; CFI_ADJUST(-8); \
        popq    %r15; CFI_ADJUST(-8); \
        popq    %r14; CFI_ADJUST(-8); \
        popq    %r13; CFI_ADJUST(-8); \
        popq    %r12; CFI_ADJUST(-8); \
        popq    %rbp; CFI_ADJUST(-8); \
        popq    %rbx; CFI_ADJUST(-8)

#endif

#ifdef SYS_mingw64
   /* Calls from OCaml to C must reserve 32 bytes of extra stack space */
#  define PREPARE_FOR_C_CALL subq $32, %rsp; CFI_ADJUST(32)
#  define CLEANUP_AFTER_C_CALL addq $32, %rsp; CFI_ADJUST(-32)
#else
#  define PREPARE_FOR_C_CALL
#  define CLEANUP_AFTER_C_CALL
#endif

        .text

        .globl  G(caml_system__code_begin)
G(caml_system__code_begin):

/* Allocation */

FUNCTION(G(caml_call_gc_r13))
        CFI_STARTPROC
        RECORD_STACK_FRAME(0)
LBL(caml_call_gc):
#ifndef SYS_mingw64
    /* Touch the stack to trigger a recoverable segfault
       if insufficient space remains */
        subq    $32768, %rsp
        movq    %rax, 0(%rsp)
        addq    $32768, %rsp
#endif
    /* Build array of registers, save it into caml_gc_regs */
        pushq   %r11; CFI_ADJUST (8);
        pushq   %r10; CFI_ADJUST (8);
        pushq   %rbp; CFI_ADJUST (8);
#        pushq   %r13; CFI_ADJUST (8); # REENTRANTRUNTIME [FIXME: Should I really save/restore %r13?] &&&&&&&&&&&&&&&&&&&&&
        pushq   %r12; CFI_ADJUST (8);
        pushq   %r9; CFI_ADJUST (8);
        pushq   %r8; CFI_ADJUST (8);
        pushq   %rcx; CFI_ADJUST (8);
        pushq   %rdx; CFI_ADJUST (8);
        pushq   %rsi; CFI_ADJUST (8);
        pushq   %rdi; CFI_ADJUST (8);
        pushq   %rbx; CFI_ADJUST (8);
        pushq   %rax; CFI_ADJUST (8);
        STORE_VAR(%rsp, caml_gc_regs)
    /* Save caml_young_ptr, caml_exception_pointer */
        STORE_VAR(%r15, caml_young_ptr)
        STORE_VAR(%r14, caml_exception_pointer)
    /* Save floating-point registers */
        subq    $(16*8), %rsp; CFI_ADJUST (16*8);
        movsd   %xmm0, 0*8(%rsp)
        movsd   %xmm1, 1*8(%rsp)
        movsd   %xmm2, 2*8(%rsp)
        movsd   %xmm3, 3*8(%rsp)
        movsd   %xmm4, 4*8(%rsp)
        movsd   %xmm5, 5*8(%rsp)
        movsd   %xmm6, 6*8(%rsp)
        movsd   %xmm7, 7*8(%rsp)
        movsd   %xmm8, 8*8(%rsp)
        movsd   %xmm9, 9*8(%rsp)
        movsd   %xmm10, 10*8(%rsp)
        movsd   %xmm11, 11*8(%rsp)
        movsd   %xmm12, 12*8(%rsp)
        movsd   %xmm13, 13*8(%rsp)
        movsd   %xmm14, 14*8(%rsp)
        movsd   %xmm15, 15*8(%rsp)
    /* Call the garbage collector */
        PREPARE_FOR_C_CALL
        movq    %r13, %rdi      /* set ctx in first argument REENTRANTRUNTIME */
        call    GCALL(caml_garbage_collection_r)
        CLEANUP_AFTER_C_CALL
    /* Restore caml_young_ptr, caml_exception_pointer */
        LOAD_VAR(caml_young_ptr, %r15)
        LOAD_VAR(caml_exception_pointer, %r14)
    /* Restore all regs used by the code generator */
        movsd   0*8(%rsp), %xmm0
        movsd   1*8(%rsp), %xmm1
        movsd   2*8(%rsp), %xmm2
        movsd   3*8(%rsp), %xmm3
        movsd   4*8(%rsp), %xmm4
        movsd   5*8(%rsp), %xmm5
        movsd   6*8(%rsp), %xmm6
        movsd   7*8(%rsp), %xmm7
        movsd   8*8(%rsp), %xmm8
        movsd   9*8(%rsp), %xmm9
        movsd   10*8(%rsp), %xmm10
        movsd   11*8(%rsp), %xmm11
        movsd   12*8(%rsp), %xmm12
        movsd   13*8(%rsp), %xmm13
        movsd   14*8(%rsp), %xmm14
        movsd   15*8(%rsp), %xmm15
        addq    $(16*8), %rsp; CFI_ADJUST(-16*8)
        popq    %rax; CFI_ADJUST(-8)
        popq    %rbx; CFI_ADJUST(-8)
        popq    %rdi; CFI_ADJUST(-8)
        popq    %rsi; CFI_ADJUST(-8)
        popq    %rdx; CFI_ADJUST(-8)
        popq    %rcx; CFI_ADJUST(-8)
        popq    %r8; CFI_ADJUST(-8)
        popq    %r9; CFI_ADJUST(-8)
        popq    %r12; CFI_ADJUST(-8)
#        popq    %r13; CFI_ADJUST(-8) # REENTRANTRUNTIME [FIXME: Should I really save/restore %r13?] &&&&&&&&&&&&&&&&&&
        popq    %rbp; CFI_ADJUST(-8)
        popq    %r10; CFI_ADJUST(-8)
        popq    %r11; CFI_ADJUST(-8)
    /* Return to caller */
        ret
CFI_ENDPROC

FUNCTION(G(caml_alloc1_r13))
CFI_STARTPROC
LBL(caml_alloc1):
        subq    $16, %r15
        CMP_VAR(caml_young_limit, %r15)
        jb      LBL(100)
        ret
LBL(100):
        RECORD_STACK_FRAME(0)
        subq    $8, %rsp; CFI_ADJUST (8);
        call    LBL(caml_call_gc)
        addq    $8, %rsp; CFI_ADJUST (-8);
        jmp     LBL(caml_alloc1)
CFI_ENDPROC

FUNCTION(G(caml_alloc2_r13))
CFI_STARTPROC
LBL(caml_alloc2):
        subq    $24, %r15
        CMP_VAR(caml_young_limit, %r15)
        jb      LBL(101)
        ret
LBL(101):
        RECORD_STACK_FRAME(0)
        subq    $8, %rsp; CFI_ADJUST (8);
        call    LBL(caml_call_gc)
        addq    $8, %rsp; CFI_ADJUST (-8);
        jmp     LBL(caml_alloc2)
CFI_ENDPROC

FUNCTION(G(caml_alloc3_r13))
CFI_STARTPROC
LBL(caml_alloc3):
        subq    $32, %r15
        CMP_VAR(caml_young_limit, %r15)
        jb      LBL(102)
        ret
LBL(102):
        RECORD_STACK_FRAME(0)
        subq    $8, %rsp; CFI_ADJUST (8)
        call    LBL(caml_call_gc)
        addq    $8, %rsp; CFI_ADJUST (-8)
        jmp     LBL(caml_alloc3)
CFI_ENDPROC

FUNCTION(G(caml_allocN_r13))
CFI_STARTPROC
LBL(caml_allocN):
        pushq   %rax; CFI_ADJUST(8)        /* save desired size */
        subq    %rax, %r15
        CMP_VAR(caml_young_limit, %r15)
        jb      LBL(103)
        addq    $8, %rsp; CFI_ADJUST (-8)  /* drop desired size */
        ret
LBL(103):
        RECORD_STACK_FRAME(8)
        call    LBL(caml_call_gc)
        popq    %rax; CFI_ADJUST(-8)       /* recover desired size */
        jmp     LBL(caml_allocN)
CFI_ENDPROC

/* Call a C function from OCaml */

FUNCTION(G(caml_c_call_r13))
CFI_STARTPROC
LBL(caml_c_call):
    /* Record lowest stack address and return address */
        popq    %r12; CFI_ADJUST(-8)
        STORE_VAR(%r12, caml_last_return_address)
        STORE_VAR(%rsp, caml_bottom_of_stack)
        subq    $8, %rsp; CFI_ADJUST(8) /* equivalent to pushq %r12 */
#ifndef SYS_mingw64
    /* Touch the stack to trigger a recoverable segfault
       if insufficient space remains */
        subq    $32768, %rsp
        movq    %rax, 0(%rsp)
        addq    $32768, %rsp
#endif
    /* Make the exception handler and alloc ptr available to the C code */
        STORE_VAR(%r15, caml_young_ptr)
        STORE_VAR(%r14, caml_exception_pointer)
    /* Call the function (address in %rax) */
    /* No need to PREPARE_FOR_C_CALL since the caller already
       reserved the stack space if needed (cf. amd64/proc.ml) */
        jmp    *%rax
CFI_ENDPROC

/* Start the OCaml program */

FUNCTION(G(caml_start_program_r))
       CFI_STARTPROC
    /* Save callee-save registers */
        PUSH_CALLEE_SAVE_REGS
    /* Initial entry point is G(caml_program) */
        leaq    GCALL(caml_program_r13)(%rip), %r12
    /* Common code for caml_start_program and caml_callback* */
LBL(caml_start_program):
    /* Build a callback link */
        subq    $8, %rsp; CFI_ADJUST (8)        /* stack 16-aligned */
        movq	%rdi, %r13      /* setup context REENTRANTRUNTIME */
        PUSH_VAR(caml_gc_regs)
        PUSH_VAR(caml_last_return_address)
        PUSH_VAR(caml_bottom_of_stack)
# FIXME: I feel uneasy about this change, which I can't test yet.  This is the old version,
# using %r13 as a temporary.  REENTRANTRUNTIME --Luca Saiu
#   /* Setup alloc ptr and exception ptr */
#       LOAD_VAR(caml_young_ptr, %r15)
#       LOAD_VAR(caml_exception_pointer, %r14)
#   /* Build an exception handler */
#       lea     LBL(108)(%rip), %r13
#       pushq   %r13; CFI_ADJUST(8)
#       pushq   %r14; CFI_ADJUST(8)
# Tentative change: begin [so we don't clobber %r13]
    /* Setup exception ptr */
        LOAD_VAR(caml_exception_pointer, %r14)
    /* Build an exception handler */
        lea     LBL(108)(%rip), %r15 # Here we use %r15 as an ordinary temporary
        pushq   %r15; CFI_ADJUST(8) # Here we use %r15 as an ordinary temporary
        pushq   %r14; CFI_ADJUST(8)
    /* Setup alloc ptr */
#check0: ############################
        LOAD_VAR(caml_young_ptr, %r15)
# Tentative change: end [so we don't clobber %r13]
        CFI_ADJUST(16)
        movq    %rsp, %r14
    /* Call the OCaml code */
        call    *%r12
LBL(107):
    /* Pop the exception handler */
        popq    %r14; CFI_ADJUST(-8)
        popq    %r12; CFI_ADJUST(-8)   /* dummy register */
        CFI_ADJUST(-16)
LBL(109):
    /* Update alloc ptr and exception ptr */
        STORE_VAR(%r15,caml_young_ptr)
        STORE_VAR(%r14,caml_exception_pointer)
    /* Pop the callback link, restoring the global variables */
        POP_VAR(caml_bottom_of_stack)
        POP_VAR(caml_last_return_address)
        POP_VAR(caml_gc_regs)
        addq    $8, %rsp; CFI_ADJUST (-8);
    /* Restore callee-save registers. */
        POP_CALLEE_SAVE_REGS
    /* Return to caller. */
        ret
LBL(108):
    /* Exception handler*/
    /* Mark the bucket as an exception result and return it */
        orq     $2, %rax
        jmp     LBL(109)
CFI_ENDPROC

/* Registers holding arguments of C functions. */

#ifdef SYS_mingw64
#define C_ARG_1 %rcx
#define C_ARG_2 %rdx
#define C_ARG_3 %r8
#define C_ARG_4 %r9
#else
#define C_ARG_1 %rdi
#define C_ARG_2 %rsi
#define C_ARG_3 %rdx
#define C_ARG_4 %rcx
#define C_ARG_5 %r8   // REENTRANTRUNTIME
#endif

/* Raise an exception from OCaml */

FUNCTION(G(caml_raise_exn_r13))
CFI_STARTPROC
        TESTL_VAR($1, caml_backtrace_active)
        jne     LBL(110)
        movq    %r14, %rsp
        popq    %r14; CFI_ADJUST(-8)
        ret
LBL(110):
        movq    %rax, %r12            /* Save exception bucket */
# Original version REENTRANTRUNTIME: begin
        ;; movq    %rax, C_ARG_1         /* arg 1: exception bucket */
        ;; popq    C_ARG_2               /* arg 2: pc of raise */
        ;; movq    %rsp, C_ARG_3         /* arg 3: sp at raise */
        ;; movq    %r14, C_ARG_4         /* arg 4: sp of handler */ 
# Original version REENTRANTRUNTIME: end
# Updated version REENTRANTRUNTIME: begin
        movq    %r13, C_ARG_1         /* arg 1: context */
        movq    %rax, C_ARG_2         /* arg 2: exception bucket */
        popq    C_ARG_3               /* arg 3: pc of raise */
        movq    %rsp, C_ARG_4         /* arg 4: sp at raise */
        movq    %r14, C_ARG_5         /* arg 5: sp of handler */
# Updated version REENTRANTRUNTIME: end
	/* PR#5700: thanks to popq above, stack is now 16-aligned */
        PREPARE_FOR_C_CALL            /* no need to cleanup after */
        call    GCALL(caml_stash_backtrace_r)
        movq    %r12, %rax            /* Recover exception bucket */
        movq    %r14, %rsp
        popq    %r14; CFI_ADJUST(-8)
        ret
CFI_ENDPROC

/* Raise an exception from C */

/* REENTRANTRUNTIME: */
/*	C arg1 : %rdi = ctx */
/*	C arg2 : %rsi = exn */
FUNCTION(G(caml_raise_exception_r))
CFI_STARTPROC
	movq 	C_ARG_1, %r13    /* setup context REENTRANTRUNTIME */
	movq	C_ARG_2, C_ARG_1 /* restore exn in first argument REENTRANTRUNTIME */
        TESTL_VAR($1, caml_backtrace_active)
        jne     LBL(111)
        movq    C_ARG_1, %rax
        LOAD_VAR(caml_exception_pointer, %rsp)  /* Cut stack */
        popq    %r14; CFI_ADJUST(-8)   /* Recover previous exception handler */
        LOAD_VAR(caml_young_ptr, %r15) /* Reload alloc ptr */
        ret
LBL(111):
        movq    C_ARG_1, %r12            /* Save exception bucket */
        
# Original code: begin REENTRANTRUNTIME
#                                      /* arg 1: exception bucket */
#        LOAD_VAR(caml_last_return_address,C_ARG_2)   /* arg 2: pc of raise */
#        LOAD_VAR(caml_bottom_of_stack,C_ARG_3)       /* arg 3: sp of raise */
#        LOAD_VAR(caml_exception_pointer,C_ARG_4)     /* arg 4: sp of handler */
# Original code: end REENTRANTRUNTIME
# Updated code: begin REENTRANTRUNTIME
        movq %r13, C_ARG_1                           /* arg 1: ctx */
        movq %r12, C_ARG_2                           /* arg 2: exception bucket */
        LOAD_VAR(caml_last_return_address, C_ARG_3)  /* arg 3: pc of raise */
        LOAD_VAR(caml_bottom_of_stack, C_ARG_4)      /* arg 4: sp of raise */
        LOAD_VAR(caml_exception_pointer, C_ARG_5)    /* arg 5: sp of handler */
# Updated code: end REENTRANTRUNTIME
        subq    $8, %rsp              /* PR#5700: maintain stack alignment */
        PREPARE_FOR_C_CALL            /* no need to cleanup after */
        call    GCALL(caml_stash_backtrace_r)
        movq    %r12, %rax            /* Recover exception bucket */
        LOAD_VAR(caml_exception_pointer,%rsp)
        popq    %r14; CFI_ADJUST(-8)   /* Recover previous exception handler */
        LOAD_VAR(caml_young_ptr,%r15)  /* Reload alloc ptr */
        ret
CFI_ENDPROC

/* Callback from C to OCaml */

/*  C arg 1: %rdi : ctx */
/*  C arg 2: %rsi : closure */
/*  C arg 3: %rdx : arg1 */
FUNCTION(G(caml_callback_exn_r))
CFI_STARTPROC
    /* Save callee-save registers */
        PUSH_CALLEE_SAVE_REGS
    /* Initial loading of arguments */
# Original code begin REENTRANTRUNTIME
#        movq    C_ARG_1, %rbx      /* closure */
#        movq    C_ARG_2, %rax      /* argument */
# Original code end REENTRANTRUNTIME
# Udpdated code begin REENTRANTRUNTIME
        movq    C_ARG_1, %r13      /* ctx */
        movq    C_ARG_2, %rbx      /* caml arg 1: closure */  // FIXME: why are %rax and %rbx reversed?
        movq    C_ARG_3, %rax      /* caml arg 2: argument */ // FIXME: why are %rax and %rbx reversed?
# Udpdated code end REENTRANTRUNTIME
        movq    0(%rbx), %r12      /* code pointer */
        jmp     LBL(caml_start_program)
CFI_ENDPROC

/*  C arg 1: %rdi : ctx */
/*  C arg 2: %rsi : closure */
/*  C arg 3: %rdx : arg1 */
/*  C arg 4: %rcx : arg2 */
FUNCTION(G(caml_callback2_exn_r))
CFI_STARTPROC
    /* Save callee-save registers */
        PUSH_CALLEE_SAVE_REGS
    /* Initial loading of arguments */
# Original code REENTRANTRUNTIME begin
#        movq    C_ARG_1, %rdi      /* closure -- no op with Unix conventions */
#        movq    C_ARG_2, %rax      /* first argument */
#        movq    C_ARG_3, %rbx      /* second argument */
# Original code REENTRANTRUNTIME end
# Updated code REENTRANTRUNTIME begin
        movq    C_ARG_1, %r13      /* setup ctx */
        movq    C_ARG_2, %rdi      /* caml arg 3: closure [this clobbers C_ARG_1, which is ok] */
        movq    C_ARG_3, %rax      /* caml arg 1: first argument */
        movq    C_ARG_4, %rbx      /* caml arg 2: second argument */
# Updated code REENTRANTRUNTIME end

        leaq    GCALL(caml_apply2)(%rip), %r12  /* code pointer */
        jmp     LBL(caml_start_program)
CFI_ENDPROC

/*  C arg 1: %rdi : ctx */
/*  C arg 2: %rsi : closure */
/*  C arg 3: %rdx : arg1 */
/*  C arg 4: %rcx : arg2 */
/*  C arg 5: %r8  : arg3 */
FUNCTION(G(caml_callback3_exn_r))
CFI_STARTPROC
    /* Save callee-save registers */
        PUSH_CALLEE_SAVE_REGS
    /* Initial loading of arguments */
# Original version begin REENTRANTRUNTIME
#        movq    C_ARG_2, %rax      /* first argument */
#        movq    C_ARG_3, %rbx      /* second argument */
#        movq    C_ARG_1, %rsi      /* closure */
#        movq    C_ARG_4, %rdi      /* third argument */
# Original version end REENTRANTRUNTIME
# Updated version begin REENTRANTRUNTIME
        movq    C_ARG_1, %r13      /* setup context */
#        movq    C_ARG_2, %rsi      /* caml arg 4: closure [actually a noop: this would clobber C_ARG_2] */
        movq    C_ARG_3, %rax      /* caml arg 1: first argument */
        movq    C_ARG_4, %rbx      /* caml arg 2: second argument */
        movq    C_ARG_5, %rdi      /* caml arg 3: third argument [this clobbers C_ARG_1] */
# Updated version end REENTRANTRUNTIME
        leaq    GCALL(caml_apply3)(%rip), %r12  /* code pointer */
        jmp     LBL(caml_start_program)
CFI_ENDPROC

FUNCTION(G(caml_ml_array_bound_error_r13))
CFI_STARTPROC
	movq 	%r13, %rdi  /* setup context as first argument REENTRANTRUNTIME*/
        leaq    GCALL(caml_array_bound_error_r)(%rip), %rax
        jmp     LBL(caml_c_call)
CFI_ENDPROC

        .globl  G(caml_system__code_end)
G(caml_system__code_end):

        .data
        .globl  G(caml_system__frametable)
        .align  EIGHT_ALIGN
G(caml_system__frametable):
        .quad   1           /* one descriptor */
        .quad   LBL(107)    /* return address into callback */
        .value  -1          /* negative frame size => use callback link */
        .value  0           /* no roots here */
        .align  EIGHT_ALIGN

#if defined(SYS_macosx)
        .literal16
#elif defined(SYS_mingw64)
        .section .rdata,"dr"
#else
        .section    .rodata.cst8,"a",@progbits
#endif
        .globl  G(caml_negf_mask)
        .align  SIXTEEN_ALIGN
G(caml_negf_mask):
        .quad   0x8000000000000000, 0
        .globl  G(caml_absf_mask)
        .align  SIXTEEN_ALIGN
G(caml_absf_mask):
        .quad   0x7FFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF

#if defined(SYS_linux)
    /* Mark stack as non-executable, PR#4564 */
        .section .note.GNU-stack,"",%progbits
#endif
