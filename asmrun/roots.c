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

/* To walk the memory roots for garbage collection */

#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_MINOR_GC

#include "finalise.h"
#include "globroots.h"
#include "memory.h"
#include "major_gc.h"
#include "minor_gc.h"
#include "misc.h"
#include "mlvalues.h"
#include "stack.h"
#include "roots.h"
#include <string.h>
#include <stdio.h>

/* The hashtable of frame descriptors */

frame_descr ** caml_frame_descriptors = NULL;
int caml_frame_descriptors_mask;

static caml_link *cons(void *data, caml_link *tl) {
  caml_link *lnk = caml_stat_alloc(sizeof(caml_link));
  lnk->data = data;
  lnk->next = tl;
  return lnk;
}

#define iter_list(list,lnk) \
  for (lnk = list; lnk != NULL; lnk = lnk->next)

/* Linked-list of frametables */

static caml_link *frametables = NULL;

void caml_register_frametable_r(CAML_R, intnat *table) {
caml_acquire_global_lock();
//fprintf(stderr, "$$$$$ Context %p: caml_register_frametable_r (table is at %p)\n", ctx, table);
  frametables = cons(table,frametables);

  if (NULL != caml_frame_descriptors) {
    caml_stat_free(caml_frame_descriptors);
    caml_frame_descriptors = NULL;
    /* force caml_init_frame_descriptors to be called */
  }
caml_release_global_lock();
}

void caml_init_frame_descriptors_r(CAML_R)
{
  //fprintf(stderr, "$$$$$ Context %p: caml_init_frame_descriptors_r: BEGIN\n", ctx);
caml_acquire_global_lock();
  intnat num_descr, tblsize, i, j, len;
  intnat * tbl;
  frame_descr * d;
  uintnat nextd;
  uintnat h;
  caml_link *lnk;

  static int inited = 0;

  if (!inited) {
    for (i = 0; caml_frametable[i] != 0; i++)
      caml_register_frametable_r(ctx, caml_frametable[i]);
    inited = 1;
  }

  /* Count the frame descriptors */
  num_descr = 0;
  iter_list(frametables,lnk) {
    num_descr += *((intnat*) lnk->data);
  }

  /* The size of the hashtable is a power of 2 greater or equal to
     2 times the number of descriptors */
  tblsize = 4;
  while (tblsize < 2 * num_descr) tblsize *= 2;

  /* Allocate the hash table */
  caml_frame_descriptors =
    (frame_descr **) caml_stat_alloc(tblsize * sizeof(frame_descr *));
  for (i = 0; i < tblsize; i++) caml_frame_descriptors[i] = NULL;
  caml_frame_descriptors_mask = tblsize - 1;

  /* Fill the hash table */
  iter_list(frametables,lnk) {
    tbl = (intnat*) lnk->data;
    len = *tbl;
    d = (frame_descr *)(tbl + 1);
    for (j = 0; j < len; j++) {
      h = Hash_retaddr(d->retaddr);
      while (caml_frame_descriptors[h] != NULL) {
        h = (h+1) & caml_frame_descriptors_mask;
      }
      caml_frame_descriptors[h] = d;
      nextd =
        ((uintnat)d +
         sizeof(char *) + sizeof(short) + sizeof(short) +
         sizeof(short) * d->num_live + sizeof(frame_descr *) - 1)
        & -sizeof(frame_descr *);
      if (d->frame_size & 1) nextd += 8;
      d = (frame_descr *) nextd;
    }
  }
caml_release_global_lock();
//fprintf(stderr, "$$$$$ Context %p: caml_init_frame_descriptors_r: END\n", ctx);
}

void caml_register_dyn_global_r(CAML_R, void *v) {
  /* No synchronization needed: this is context-local */
  caml_dyn_globals = cons((void*) v,caml_dyn_globals);
}

/* Call [caml_oldify_one] on (at least) all the roots that point to the minor
   heap. */
void caml_oldify_local_roots_r (CAML_R)
{
caml_acquire_global_lock();
  char * sp;
  uintnat retaddr;
  value * regs;
  frame_descr * d;
  uintnat h;
  int i, j, n, ofs;
#ifdef Stack_grows_upwards
  short * p;  /* PR#4339: stack offsets are negative in this case */
#else
  unsigned short * p;
#endif
  value glob;
  value * root;
  struct caml__roots_block *lr;
  caml_link *lnk;

  /* The global roots */
  for (i = caml_globals_scanned;
       i <= caml_globals_inited && caml_globals[i] != 0;
       i++) {
    glob = caml_globals[i];
    for (j = 0; j < Wosize_val(glob); j++){
      Oldify (&Field (glob, j));
    }
  }
  caml_globals_scanned = caml_globals_inited;

  /* Dynamic global roots */
  iter_list(caml_dyn_globals, lnk) {
    glob = (value) lnk->data;
    for (j = 0; j < Wosize_val(glob); j++){
      Oldify (&Field (glob, j));
    }
  }

  /* The stack and local roots */
  if (caml_frame_descriptors == NULL) caml_init_frame_descriptors_r(ctx);
  sp = caml_bottom_of_stack;
  retaddr = caml_last_return_address;
  regs = caml_gc_regs;
  if (sp != NULL) {
    while (1) {
      /* Find the descriptor corresponding to the return address */
      h = Hash_retaddr(retaddr);
      while(1) {
        d = caml_frame_descriptors[h];
        if (d->retaddr == retaddr) break;
        h = (h+1) & caml_frame_descriptors_mask;
      }
      if (d->frame_size != 0xFFFF) {
        /* Scan the roots in this frame */
        for (p = d->live_ofs, n = d->num_live; n > 0; n--, p++) {
          ofs = *p;
          if (ofs & 1) {
            root = regs + (ofs >> 1);
          } else {
            root = (value *)(sp + ofs);
          }
          Oldify (root);
        }
        /* Move to next frame */
#ifndef Stack_grows_upwards
        sp += (d->frame_size & 0xFFFC);
#else
        sp -= (d->frame_size & 0xFFFC);
#endif
        retaddr = Saved_return_address(sp);
#ifdef Already_scanned
        /* Stop here if the frame has been scanned during earlier GCs  */
        if (Already_scanned(sp, retaddr)) break;
        /* Mark frame as already scanned */
        Mark_scanned(sp, retaddr);
#endif
      } else {
        /* This marks the top of a stack chunk for an ML callback.
           Skip C portion of stack and continue with next ML stack chunk. */
        struct caml_context * next_context = Callback_link(sp);
        sp = next_context->bottom_of_stack;
        retaddr = next_context->last_retaddr;
        regs = next_context->gc_regs;
        /* A null sp means no more ML stack chunks; stop here. */
        if (sp == NULL) break;
      }
    }
  }
  /* Local C roots */
  for (lr = caml_local_roots; lr != NULL; lr = lr->next) {
DUMP("lr is %p", lr);
DUMP("lr->ntables is %i", lr->ntables);
    for (i = 0; i < lr->ntables; i++){
      for (j = 0; j < lr->nitems; j++){
        root = &(lr->tables[i][j]);
        Oldify (root);
      }
    }
  }
  /* Global C roots */
  caml_scan_global_young_roots_r(ctx, &caml_oldify_one_r);
  /* Finalised values */
  caml_final_do_young_roots_r (ctx, &caml_oldify_one_r);
  /* Hook */
  if (caml_scan_roots_hook != NULL) (*caml_scan_roots_hook)(&caml_oldify_one_r);
caml_release_global_lock();
}

/* Call [darken] on all roots */

void caml_darken_all_roots_r (CAML_R)
{
  caml_do_roots_r (ctx, caml_darken_r);
}

void caml_do_roots_r (CAML_R, scanning_action f)
{
caml_acquire_global_lock();
  int i, j;
  value glob;
  caml_link *lnk;

  /* The global roots */
  for (i = 0; caml_globals[i] != 0; i++) {
    glob = caml_globals[i];
    for (j = 0; j < Wosize_val(glob); j++)
      f (ctx, Field (glob, j), &Field (glob, j));
  }

  /* Dynamic global roots */
  iter_list(caml_dyn_globals, lnk) {
    glob = (value) lnk->data;
    for (j = 0; j < Wosize_val(glob); j++){
      f (ctx, Field (glob, j), &Field (glob, j));
    }
  }

  /* The stack and local roots */
  if (caml_frame_descriptors == NULL) caml_init_frame_descriptors_r(ctx);
  caml_do_local_roots_r(ctx, f, caml_bottom_of_stack, caml_last_return_address,
                      caml_gc_regs, caml_local_roots);
  /* Global C roots */
  caml_scan_global_roots_r(ctx, f);
  /* Finalised values */
  caml_final_do_strong_roots_r (ctx, f);
  /* Hook */
  if (caml_scan_roots_hook != NULL) (*caml_scan_roots_hook)(f);
caml_release_global_lock();
}

void caml_do_local_roots_r(CAML_R, scanning_action f, char * bottom_of_stack,
                         uintnat last_retaddr, value * gc_regs,
                         struct caml__roots_block * local_roots)
{
caml_acquire_global_lock();
  char * sp;
  uintnat retaddr;
  value * regs;
  frame_descr * d;
  uintnat h;
  int i, j, n, ofs;
#ifdef Stack_grows_upwards
  short * p;  /* PR#4339: stack offsets are negative in this case */
#else
  unsigned short * p;
#endif
  value * root;
  struct caml__roots_block *lr;

  sp = bottom_of_stack;
  retaddr = last_retaddr;
  regs = gc_regs;
  if (sp != NULL) {
    while (1) {
      /* Find the descriptor corresponding to the return address */
      h = Hash_retaddr(retaddr);
      while(1) {
        d = caml_frame_descriptors[h];
        if (d->retaddr == retaddr) break;
        h = (h+1) & caml_frame_descriptors_mask;
      }
      if (d->frame_size != 0xFFFF) {
        /* Scan the roots in this frame */
        for (p = d->live_ofs, n = d->num_live; n > 0; n--, p++) {
          ofs = *p;
          if (ofs & 1) {
            root = regs + (ofs >> 1);
          } else {
            root = (value *)(sp + ofs);
          }
          f (ctx, *root, root);
        }
        /* Move to next frame */
#ifndef Stack_grows_upwards
        sp += (d->frame_size & 0xFFFC);
#else
        sp -= (d->frame_size & 0xFFFC);
#endif
        retaddr = Saved_return_address(sp);
#ifdef Mask_already_scanned
        retaddr = Mask_already_scanned(retaddr);
#endif
      } else {
        /* This marks the top of a stack chunk for an ML callback.
           Skip C portion of stack and continue with next ML stack chunk. */
        struct caml_context * next_context = Callback_link(sp);
        sp = next_context->bottom_of_stack;
        retaddr = next_context->last_retaddr;
        regs = next_context->gc_regs;
        /* A null sp means no more ML stack chunks; stop here. */
        if (sp == NULL) break;
      }
    }
  }
  /* Local C roots */
  for (lr = local_roots; lr != NULL; lr = lr->next) {
DUMP("lr is %p", lr);
DUMP("lr->ntables is %i", lr->ntables);
    for (i = 0; i < lr->ntables; i++){
      for (j = 0; j < lr->nitems; j++){
        root = &(lr->tables[i][j]);
        f (ctx, *root, root);
      }
    }
  }
caml_release_global_lock();
}

uintnat caml_stack_usage_r (CAML_R)
{
  uintnat sz;
caml_acquire_global_lock();
  sz = (value *) caml_top_of_stack - (value *) caml_bottom_of_stack;
  if (caml_stack_usage_hook != NULL)
    sz += (*caml_stack_usage_hook)();
caml_release_global_lock();
  return sz;
}

CAMLprim value caml_incr_globals_inited_r(CAML_R)
{
  caml_globals_inited++;
  return Val_unit;
}
