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

/* Structured input, compact format */

/* The interface of this file is "intext.h" */

#define CAML_CONTEXT_STARTUP
#define CAML_CONTEXT_MAJOR_GC
#define CAML_CONTEXT_INTERN
#define CAML_CONTEXT_ROOTS

#include <string.h>
#include <stdio.h>
#include "alloc.h"
#include "callback.h"
#include "custom.h"
#include "fail.h"
#include "gc.h"
#include "intext.h"
#include "io.h"
#include "md5.h"
#include "memory.h"
#include "mlvalues.h"
#include "misc.h"
#include "reverse.h"

static char * intern_resolve_code_pointer_r(CAML_R, unsigned char digest[16],
                                            asize_t offset);
static void intern_bad_code_pointer_r(CAML_R, unsigned char digest[16]) Noreturn;

static void intern_free_stack_r(CAML_R);

#define Sign_extend_shift ((sizeof(intnat) - 1) * 8)
#define Sign_extend(x) (((intnat)(x) << Sign_extend_shift) >> Sign_extend_shift)

#define read8u() (*intern_src++)
#define read8s() Sign_extend(*intern_src++)
#define read16u() \
  (intern_src += 2, \
   (intern_src[-2] << 8) + intern_src[-1])
#define read16s() \
  (intern_src += 2, \
   (Sign_extend(intern_src[-2]) << 8) + intern_src[-1])
#define read32u() \
  (intern_src += 4, \
   ((uintnat)(intern_src[-4]) << 24) + (intern_src[-3] << 16) + \
   (intern_src[-2] << 8) + intern_src[-1])
#define read32s() \
  (intern_src += 4, \
   (Sign_extend(intern_src[-4]) << 24) + (intern_src[-3] << 16) + \
   (intern_src[-2] << 8) + intern_src[-1])

#ifdef ARCH_SIXTYFOUR
static intnat read64s_r(CAML_R)
{
  intnat res;
  int i;
  res = 0;
  for (i = 0; i < 8; i++) res = (res << 8) + intern_src[i];
  intern_src += 8;
  return res;
}
#endif

#define readblock(dest,len) \
  (memmove((dest), intern_src, (len)), intern_src += (len))

static void intern_cleanup_r(CAML_R)
{
  if (intern_input_malloced) caml_stat_free(intern_input);
  if (intern_obj_table != NULL) caml_stat_free(intern_obj_table);
  if (intern_extra_block != NULL) {
    /* free newly allocated heap chunk */
    caml_free_for_heap(intern_extra_block);
  } else if (intern_block != 0) {
    /* restore original header for heap block, otherwise GC is confused */
    Hd_val(intern_block) = intern_header;
  }
  /* free the recursion stack */
  intern_free_stack_r(ctx);
}

static void readfloat_r(CAML_R, double * dest, unsigned int code)
{
  if (sizeof(double) != 8) {
    intern_cleanup_r(ctx);
    caml_invalid_argument_r(ctx, "input_value: non-standard floats");
  }
  readblock((char *) dest, 8);
  /* Fix up endianness, if needed */
#if ARCH_FLOAT_ENDIANNESS == 0x76543210
  /* Host is big-endian; fix up if data read is little-endian */
  if (code != CODE_DOUBLE_BIG) Reverse_64(dest, dest);
#elif ARCH_FLOAT_ENDIANNESS == 0x01234567
  /* Host is little-endian; fix up if data read is big-endian */
  if (code != CODE_DOUBLE_LITTLE) Reverse_64(dest, dest);
#else
  /* Host is neither big nor little; permute as appropriate */
  if (code == CODE_DOUBLE_LITTLE)
    Permute_64(dest, ARCH_FLOAT_ENDIANNESS, dest, 0x01234567)
  else
    Permute_64(dest, ARCH_FLOAT_ENDIANNESS, dest, 0x76543210);
#endif
}

static void readfloats_r(CAML_R, double * dest, mlsize_t len, unsigned int code)
{
  mlsize_t i;
  if (sizeof(double) != 8) {
    intern_cleanup_r(ctx);
    caml_invalid_argument_r(ctx, "input_value: non-standard floats");
  }
  readblock((char *) dest, len * 8);
  /* Fix up endianness, if needed */
#if ARCH_FLOAT_ENDIANNESS == 0x76543210
  /* Host is big-endian; fix up if data read is little-endian */
  if (code != CODE_DOUBLE_ARRAY8_BIG &&
      code != CODE_DOUBLE_ARRAY32_BIG) {
    for (i = 0; i < len; i++) Reverse_64(dest + i, dest + i);
  }
#elif ARCH_FLOAT_ENDIANNESS == 0x01234567
  /* Host is little-endian; fix up if data read is big-endian */
  if (code != CODE_DOUBLE_ARRAY8_LITTLE &&
      code != CODE_DOUBLE_ARRAY32_LITTLE) {
    for (i = 0; i < len; i++) Reverse_64(dest + i, dest + i);
  }
#else
  /* Host is neither big nor little; permute as appropriate */
  if (code == CODE_DOUBLE_ARRAY8_LITTLE ||
      code == CODE_DOUBLE_ARRAY32_LITTLE) {
    for (i = 0; i < len; i++)
      Permute_64(dest + i, ARCH_FLOAT_ENDIANNESS, dest + i, 0x01234567);
  } else {
    for (i = 0; i < len; i++)
      Permute_64(dest + i, ARCH_FLOAT_ENDIANNESS, dest + i, 0x76543210);
  }
#endif
}

/* /\* Item on the stack with defined operation *\/ */
/* struct intern_item { */
/*   value * dest; */
/*   intnat arg; */
/*   enum { */
/*     OReadItems, /\* read arg items and store them in dest[0], dest[1], ... *\/ */
/*     OFreshOID,  /\* generate a fresh OID and store it in *dest *\/ */
/*     OShift      /\* offset *dest by arg *\/ */
/*   } op; */
/* }; */

/* FIXME: This is duplicated in two other places, with the only difference of
   the type of elements stored in the stack. Possible solution in C would
   be to instantiate stack these function via. C preprocessor macro.
 */

//#define INTERN_STACK_INIT_SIZE 256
#define INTERN_STACK_MAX_SIZE (1024*1024*100)

/* static struct intern_item intern_stack_init[INTERN_STACK_INIT_SIZE]; */

/* static struct intern_item * intern_stack = intern_stack_init; */
/* static struct intern_item * intern_stack_limit = intern_stack_init */
/*                                                    + INTERN_STACK_INIT_SIZE; */

/* Free the recursion stack if needed */
static void intern_free_stack_r(CAML_R)
{
  if (intern_stack != intern_stack_init) {
    free(intern_stack);
    /* Reinitialize the globals for next time around */
    intern_stack = intern_stack_init;
    intern_stack_limit = intern_stack + INTERN_STACK_INIT_SIZE;
  }
}

/* Same, then raise Out_of_memory */
static void intern_stack_overflow_r(CAML_R)
{
  caml_gc_message (0x04, "Stack overflow in un-marshaling value\n", 0);
  intern_free_stack_r(ctx);
  caml_raise_out_of_memory_r(ctx);
}

static struct intern_item * intern_resize_stack_r(CAML_R, struct intern_item * sp)
{
  asize_t newsize = 2 * (intern_stack_limit - intern_stack);
  asize_t sp_offset = sp - intern_stack;
  struct intern_item * newstack;

  if (newsize >= INTERN_STACK_MAX_SIZE) intern_stack_overflow_r(ctx);
  if (intern_stack == intern_stack_init) {
    newstack = malloc(sizeof(struct intern_item) * newsize);
    if (newstack == NULL) intern_stack_overflow_r(ctx);
    memcpy(newstack, intern_stack_init,
           sizeof(struct intern_item) * INTERN_STACK_INIT_SIZE);
  } else {
    newstack =
      realloc(intern_stack, sizeof(struct intern_item) * newsize);
    if (newstack == NULL) intern_stack_overflow_r(ctx);
  }
  intern_stack = newstack;
  intern_stack_limit = newstack + newsize;
  return newstack + sp_offset;
}

/* Convenience macros for requesting operation on the stack */
#define PushItem()                                                      \
  do {                                                                  \
    sp++;                                                               \
    if (sp >= intern_stack_limit) sp = intern_resize_stack_r(ctx, sp);  \
  } while(0)

#define ReadItems(_dest,_n)                                             \
  do {                                                                  \
    if (_n > 0) {                                                       \
      PushItem();                                                       \
      sp->op = OReadItems;                                              \
      sp->dest = _dest;                                                 \
      sp->arg = _n;                                                     \
    }                                                                   \
  } while(0)

static void intern_rec_r(CAML_R, value *dest)
{
  unsigned int code;
  tag_t tag;
  mlsize_t size, len, ofs_ind;
  value v;
  asize_t ofs;
  header_t header;
  unsigned char digest[16];
  struct custom_operations * ops;
  char * codeptr;
  struct intern_item * sp;

  sp = intern_stack;

  /* Initially let's try to read the first object from the stream */
  ReadItems(dest, 1);

  /* The un-marshaler loop, the recursion is unrolled */
  while(sp != intern_stack) {

  /* Interpret next item on the stack */
  dest = sp->dest;
  switch (sp->op) {
  case OFreshOID:
    /* Refresh the object ID */
    if (camlinternaloo_last_id == NULL) {
      camlinternaloo_last_id = caml_named_value_r(ctx, "CamlinternalOO.last_id");
      if (camlinternaloo_last_id == NULL)
        camlinternaloo_last_id = (value*) (-1);
    }
    if (camlinternaloo_last_id != (value*) (-1)) {
      value id = Field(*camlinternaloo_last_id,0);
      Field(dest, 0) = id;
      Field(*camlinternaloo_last_id,0) = id + 2;
    }
    /* Pop item and iterate */
    sp--;
    break;
  case OShift:
    /* Shift value by an offset */
    *dest += sp->arg;
    /* Pop item and iterate */
    sp--;
    break;
  case OReadItems:
    /* Pop item */
    sp->dest++;
    if (--(sp->arg) == 0) sp--;
    /* Read a value and set v to this value */
  code = read8u();
  if (code >= PREFIX_SMALL_INT) {
    if (code >= PREFIX_SMALL_BLOCK) {
      /* Small block */
      tag = code & 0xF;
      size = (code >> 4) & 0x7;
    read_block:
      if (size == 0) {
        v = Atom(tag);
      } else {
        v = Val_hp(intern_dest);
        if (intern_obj_table != NULL) intern_obj_table[obj_counter++] = v;
        *intern_dest = Make_header(size, tag, intern_color);
        intern_dest += 1 + size;
        /* For objects, we need to freshen the oid */
        if (tag == Object_tag) {
          Assert(size >= 2);
          /* Request to read rest of the elements of the block */
          ReadItems(&Field(v, 2), size - 2);
          /* Request freshing OID */
          PushItem();
          sp->op = OFreshOID;
          sp->dest = &Field(v, 1);
          sp->arg = 1;
          /* Finally read first two block elements: method table and old OID */
          ReadItems(&Field(v, 0), 2);
        } else
          /* If it's not an object then read the contents of the block */
          ReadItems(&Field(v, 0), size);
      }
    } else {
      /* Small integer */
      v = Val_int(code & 0x3F);
    }
  } else {
    if (code >= PREFIX_SMALL_STRING) {
      /* Small string */
      len = (code & 0x1F);
    read_string:
      size = (len + sizeof(value)) / sizeof(value);
      v = Val_hp(intern_dest);
      if (intern_obj_table != NULL) intern_obj_table[obj_counter++] = v;
      *intern_dest = Make_header(size, String_tag, intern_color);
      intern_dest += 1 + size;
      Field(v, size - 1) = 0;
      ofs_ind = Bsize_wsize(size) - 1;
      Byte(v, ofs_ind) = ofs_ind - len;
      readblock(String_val(v), len);
    } else {
      switch(code) {
      case CODE_INT8:
        v = Val_long(read8s());
        break;
      case CODE_INT16:
        v = Val_long(read16s());
        break;
      case CODE_INT32:
        v = Val_long(read32s());
        break;
      case CODE_INT64:
#ifdef ARCH_SIXTYFOUR
        v = Val_long(read64s_r(ctx));
        break;
#else
        intern_cleanup_r(ctx);
        caml_failwith("input_value: integer too large");
        break;
#endif
      case CODE_SHARED8:
        ofs = read8u();
      read_shared:
        Assert (ofs > 0);
        Assert (ofs <= obj_counter);
        Assert (intern_obj_table != NULL);
        v = intern_obj_table[obj_counter - ofs];
        break;
      case CODE_SHARED16:
        ofs = read16u();
        goto read_shared;
      case CODE_SHARED32:
        ofs = read32u();
        goto read_shared;
      case CODE_BLOCK32:
        header = (header_t) read32u();
        tag = Tag_hd(header);
        size = Wosize_hd(header);
        goto read_block;
      case CODE_BLOCK64:
#ifdef ARCH_SIXTYFOUR
        header = (header_t) read64s_r(ctx);
        tag = Tag_hd(header);
        size = Wosize_hd(header);
        goto read_block;
#else
        intern_cleanup_r(ctx);
        caml_failwith("input_value: data block too large");
        break;
#endif
      case CODE_STRING8:
        len = read8u();
        goto read_string;
      case CODE_STRING32:
        len = read32u();
        goto read_string;
      case CODE_DOUBLE_LITTLE:
      case CODE_DOUBLE_BIG:
        v = Val_hp(intern_dest);
        if (intern_obj_table != NULL) intern_obj_table[obj_counter++] = v;
        *intern_dest = Make_header(Double_wosize, Double_tag, intern_color);
        intern_dest += 1 + Double_wosize;
        readfloat_r(ctx, (double *) v, code);
        break;
      case CODE_DOUBLE_ARRAY8_LITTLE:
      case CODE_DOUBLE_ARRAY8_BIG:
        len = read8u();
      read_double_array:
        size = len * Double_wosize;
        v = Val_hp(intern_dest);
        if (intern_obj_table != NULL) intern_obj_table[obj_counter++] = v;
        *intern_dest = Make_header(size, Double_array_tag, intern_color);
        intern_dest += 1 + size;
        readfloats_r(ctx, (double *) v, len, code);
        break;
      case CODE_DOUBLE_ARRAY32_LITTLE:
      case CODE_DOUBLE_ARRAY32_BIG:
        len = read32u();
        goto read_double_array;
      case CODE_CODEPOINTER:
        ofs = read32u();
        readblock(digest, 16);
        codeptr = intern_resolve_code_pointer_r(ctx, digest, ofs);
        if (codeptr != NULL) {
          v = (value) codeptr;
        } else {
          value * function_placeholder =
            caml_named_value_r (ctx, "Debugger.function_placeholder");
          if (function_placeholder != NULL) {
            v = *function_placeholder;
          } else {
            intern_cleanup_r(ctx);
            intern_bad_code_pointer_r(ctx, digest);
          }
        }
        break;
      case CODE_INFIXPOINTER:
        ofs = read32u();
        /* Read a value to *dest, then offset *dest by ofs */
        PushItem();
        sp->dest = dest;
        sp->op = OShift;
        sp->arg = ofs;
        ReadItems(dest, 1);
        continue;  /* with next iteration of main loop, skipping *dest = v */
      case CODE_CUSTOM:
        ops = caml_find_custom_operations((char *) intern_src);
        if (ops == NULL) {
          fprintf(stderr, "?????LUCA SAIU[About the intern_src %p]\n", intern_src);
          fprintf(stderr, "?????LUCA SAIU[--which is to say, about a custom object of type \"%s\"]\n", intern_src);
          intern_cleanup_r(ctx);
          caml_failwith_r(ctx, "input_value: unknown custom block identifier");
        }
        while (*intern_src++ != 0) /*nothing*/;  /*skip identifier*/
        size = ops->deserialize((void *) (intern_dest + 2));
        size = 1 + (size + sizeof(value) - 1) / sizeof(value);
        v = Val_hp(intern_dest);
        if (intern_obj_table != NULL) intern_obj_table[obj_counter++] = v;
        *intern_dest = Make_header(size, Custom_tag, intern_color);
        Custom_ops_val(v) = ops;
        intern_dest += 1 + size;
        break;
      default:
        intern_cleanup_r(ctx);
        caml_failwith_r(ctx, "input_value: ill-formed message");
      }
    }
  }
  /* end of case OReadItems */
  *dest = v;
  break;
  default:
    Assert(0);
  }
  }
  /* We are done. Cleanup the stack and leave the function */
  intern_free_stack_r(ctx);
}

static void intern_alloc_r(CAML_R, mlsize_t whsize, mlsize_t num_objects)
{
  mlsize_t wosize;

  if (camlinternaloo_last_id == (value*)-1)
    camlinternaloo_last_id = NULL; /* Reset ignore flag */
  if (whsize == 0) {
    intern_obj_table = NULL;
    intern_extra_block = NULL;
    intern_block = 0;
    return;
  }
  wosize = Wosize_whsize(whsize);
  if (wosize > Max_wosize) {
    /* Round desired size up to next page */
    asize_t request =
      ((Bsize_wsize(whsize) + Page_size - 1) >> Page_log) << Page_log;
    intern_extra_block = caml_alloc_for_heap(request);
    if (intern_extra_block == NULL) caml_raise_out_of_memory_r(ctx);
    intern_color = caml_allocation_color_r(ctx, intern_extra_block);
    intern_dest = (header_t *) intern_extra_block;
  } else {
    /* this is a specialised version of caml_alloc from alloc.c */
    if (wosize == 0){
      intern_block = Atom (String_tag);
    }else if (wosize <= Max_young_wosize){
      intern_block = caml_alloc_small_r (ctx, wosize, String_tag);
    }else{
      intern_block = caml_alloc_shr_r (ctx, wosize, String_tag);
      /* do not do the urgent_gc check here because it might darken
         intern_block into gray and break the Assert 3 lines down */
    }
    intern_header = Hd_val(intern_block);
    intern_color = Color_hd(intern_header);
    Assert (intern_color == Caml_white || intern_color == Caml_black);
    intern_dest = (header_t *) Hp_val(intern_block);
    intern_extra_block = NULL;
  }
  obj_counter = 0;
  if (num_objects > 0)
    intern_obj_table = (value *) caml_stat_alloc(num_objects * sizeof(value));
  else
    intern_obj_table = NULL;
}

static void intern_add_to_heap_r(CAML_R, mlsize_t whsize)
{
  /* Add new heap chunk to heap if needed */
  if (intern_extra_block != NULL) {
    /* If heap chunk not filled totally, build free block at end */
    asize_t request =
      ((Bsize_wsize(whsize) + Page_size - 1) >> Page_log) << Page_log;
    header_t * end_extra_block =
      (header_t *) intern_extra_block + Wsize_bsize(request);
    Assert(intern_dest <= end_extra_block);
    if (intern_dest < end_extra_block){
      caml_make_free_blocks_r (ctx, (value *) intern_dest,
                               end_extra_block - intern_dest, 0, Caml_white);
    }
    caml_allocated_words +=
      Wsize_bsize ((char *) intern_dest - intern_extra_block);
    caml_add_to_heap_r(ctx, intern_extra_block);
  }
}

value caml_input_val_r(CAML_R, struct channel *chan)
{
  uint32 magic;
  mlsize_t block_len, num_objects, size_32, size_64, whsize;
  char * block;
  value res;

  if (! caml_channel_binary_mode(chan))
    caml_failwith_r(ctx,"input_value: not a binary channel");
  magic = caml_getword_r(ctx, chan);
  if (magic != Intext_magic_number) caml_failwith_r(ctx,"input_value: bad object");
  block_len = caml_getword_r(ctx, chan);
  num_objects = caml_getword_r(ctx, chan);
  size_32 = caml_getword_r(ctx, chan);
  size_64 = caml_getword_r(ctx, chan);
  /* Read block from channel */
  block = caml_stat_alloc(block_len);
  /* During [caml_really_getblock], concurrent [caml_input_val] operations
     can take place (via signal handlers or context switching in systhreads),
     and [intern_input] may change.  So, wait until [caml_really_getblock]
     is over before using [intern_input] and the other global vars. */
  if (caml_really_getblock_r(ctx, chan, block, block_len) == 0) {
    caml_stat_free(block);
    caml_failwith_r(ctx,"input_value: truncated object");
  }
  intern_input = (unsigned char *) block;
  intern_input_malloced = 1;
  intern_src = intern_input;
  /* Allocate result */
#ifdef ARCH_SIXTYFOUR
  whsize = size_64;
#else
  whsize = size_32;
#endif
  intern_alloc_r(ctx, whsize, num_objects);
  /* Fill it in */
  intern_rec_r(ctx,&res);
  intern_add_to_heap_r(ctx, whsize);
  /* Free everything */
  caml_stat_free(intern_input);
  if (intern_obj_table != NULL) caml_stat_free(intern_obj_table);
  return res;
}

CAMLprim value caml_input_value_r(CAML_R, value vchan)
{
  CAMLparam1 (vchan);
  struct channel * chan = Channel(vchan);
  CAMLlocal1 (res);

  Lock(chan);
  res = caml_input_val_r(ctx, chan);
  Unlock(chan);
  CAMLreturn (res);
}

CAMLexport value caml_input_val_from_string_r(CAML_R, value str, intnat ofs)
{
  CAMLparam1 (str);
  mlsize_t num_objects, size_32, size_64, whsize;
  CAMLlocal1 (obj);

  intern_src = &Byte_u(str, ofs + 2*4);
  intern_input_malloced = 0;
  num_objects = read32u();
  size_32 = read32u();
  size_64 = read32u();
  /* Allocate result */
#ifdef ARCH_SIXTYFOUR
  whsize = size_64;
#else
  whsize = size_32;
#endif
  intern_alloc_r(ctx, whsize, num_objects);
  intern_src = &Byte_u(str, ofs + 5*4); /* If a GC occurred */
  /* Fill it in */
  intern_rec_r(ctx,&obj);
  intern_add_to_heap_r(ctx, whsize);
  /* Free everything */
  if (intern_obj_table != NULL) caml_stat_free(intern_obj_table);
  CAMLreturn (obj);
}

CAMLprim value caml_input_value_from_string_r(CAML_R, value str, value ofs)
{
  return caml_input_val_from_string_r(ctx, str, Long_val(ofs));
}

static value input_val_from_block_r(CAML_R)
{
  mlsize_t num_objects, size_32, size_64, whsize;
  value obj;

  num_objects = read32u();
  size_32 = read32u();
  size_64 = read32u();
  /* Allocate result */
#ifdef ARCH_SIXTYFOUR
  whsize = size_64;
#else
  whsize = size_32;
#endif
  intern_alloc_r(ctx, whsize, num_objects);
  /* Fill it in */
  intern_rec_r(ctx,&obj);
  intern_add_to_heap_r(ctx, whsize);
  /* Free internal data structures */
  if (intern_obj_table != NULL) caml_stat_free(intern_obj_table);
  return obj;
}

CAMLexport value caml_input_value_from_malloc_r(CAML_R, char * data, intnat ofs)
{
  uint32 magic;
  mlsize_t block_len;
  value obj;

  intern_input = (unsigned char *) data;
  intern_src = intern_input + ofs;
  intern_input_malloced = 1;
  magic = read32u();
  if (magic != Intext_magic_number)
    caml_failwith_r(ctx,"input_value_from_malloc: bad object");
  block_len = read32u();
  obj = input_val_from_block_r(ctx);
  /* Free the input */
  caml_stat_free(intern_input);
  return obj;
}

CAMLexport value caml_input_value_from_block_r(CAML_R, char * data, intnat len)
{
  uint32 magic;
  mlsize_t block_len;
  value obj;

  intern_input = (unsigned char *) data;
  intern_src = intern_input;
  intern_input_malloced = 0;
  magic = read32u();
  if (magic != Intext_magic_number)
    caml_failwith_r(ctx,"input_value_from_block: bad object");
  block_len = read32u();
  if (5*4 + block_len > len)
    caml_failwith_r(ctx,"input_value_from_block: bad block length");
  obj = input_val_from_block_r(ctx);
  return obj;
}

CAMLprim value caml_marshal_data_size_r(CAML_R, value buff, value ofs)
{
  uint32 magic;
  mlsize_t block_len;

  intern_src = &Byte_u(buff, Long_val(ofs));
  intern_input_malloced = 0;
  magic = read32u();
  if (magic != Intext_magic_number){
    caml_failwith_r(ctx,"Marshal.data_size: bad object");
  }
  block_len = read32u();
  return Val_long(block_len);
}

/* Resolution of code pointers */

static char * intern_resolve_code_pointer_r(CAML_R, unsigned char digest[16],
                                            asize_t offset)
{
  int i;
  for (i = caml_code_fragments_table.size - 1; i >= 0; i--) {
    struct code_fragment * cf = caml_code_fragments_table.contents[i];
    if (! cf->digest_computed) {
      caml_md5_block(cf->digest, cf->code_start, cf->code_end - cf->code_start);
      cf->digest_computed = 1;
    }
    if (memcmp(digest, cf->digest, 16) == 0) {
      if (cf->code_start + offset < cf->code_end)
        return cf->code_start + offset;
      else
        return NULL;
    }
  }
  return NULL;
}

static void intern_bad_code_pointer_r(CAML_R, unsigned char digest[16])
{
  char msg[256];
  sprintf(msg, "input_value: unknown code module %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
          digest[0], digest[1], digest[2], digest[3],
          digest[4], digest[5], digest[6], digest[7],
          digest[8], digest[9], digest[10], digest[11],
          digest[12], digest[13], digest[14], digest[15]);
  caml_failwith_r(ctx, msg);
}

/* Functions for writing user-defined marshallers */

CAMLexport int caml_deserialize_uint_1_r(CAML_R)
{
  return read8u();
}

CAMLexport int caml_deserialize_sint_1_r(CAML_R)
{
  return read8s();
}

CAMLexport int caml_deserialize_uint_2_r(CAML_R)
{
  return read16u();
}

CAMLexport int caml_deserialize_sint_2_r(CAML_R)
{
  return read16s();
}

CAMLexport uint32 caml_deserialize_uint_4_r(CAML_R)
{
  return read32u();
}

CAMLexport int32 caml_deserialize_sint_4_r(CAML_R)
{
  return read32s();
}

CAMLexport uint64 caml_deserialize_uint_8_r(CAML_R)
{
  uint64 i;
  caml_deserialize_block_8_r(ctx, &i, 1);
  return i;
}

CAMLexport int64 caml_deserialize_sint_8_r(CAML_R)
{
  int64 i;
  caml_deserialize_block_8_r(ctx,&i, 1);
  return i;
}

CAMLexport float caml_deserialize_float_4_r(CAML_R)
{
  float f;
  caml_deserialize_block_4_r(ctx, &f, 1);
  return f;
}

CAMLexport double caml_deserialize_float_8_r(CAML_R)
{
  double f;
  caml_deserialize_block_float_8_r(ctx, &f, 1);
  return f;
}

CAMLexport void caml_deserialize_block_1_r(CAML_R, void * data, intnat len)
{
  memmove(data, intern_src, len);
  intern_src += len;
}

CAMLexport void caml_deserialize_block_2_r(CAML_R, void * data, intnat len)
{
#ifndef ARCH_BIG_ENDIAN
  unsigned char * p, * q;
  for (p = intern_src, q = data; len > 0; len--, p += 2, q += 2)
    Reverse_16(q, p);
  intern_src = p;
#else
  memmove(data, intern_src, len * 2);
  intern_src += len * 2;
#endif
}

CAMLexport void caml_deserialize_block_4_r(CAML_R, void * data, intnat len)
{
#ifndef ARCH_BIG_ENDIAN
  unsigned char * p, * q;
  for (p = intern_src, q = data; len > 0; len--, p += 4, q += 4)
    Reverse_32(q, p);
  intern_src = p;
#else
  memmove(data, intern_src, len * 4);
  intern_src += len * 4;
#endif
}

CAMLexport void caml_deserialize_block_8_r(CAML_R, void * data, intnat len)
{
#ifndef ARCH_BIG_ENDIAN
  unsigned char * p, * q;
  for (p = intern_src, q = data; len > 0; len--, p += 8, q += 8)
    Reverse_64(q, p);
  intern_src = p;
#else
  memmove(data, intern_src, len * 8);
  intern_src += len * 8;
#endif
}

CAMLexport void caml_deserialize_block_float_8_r(CAML_R, void * data, intnat len)
{
#if ARCH_FLOAT_ENDIANNESS == 0x01234567
  memmove(data, intern_src, len * 8);
  intern_src += len * 8;
#elif ARCH_FLOAT_ENDIANNESS == 0x76543210
  unsigned char * p, * q;
  for (p = intern_src, q = data; len > 0; len--, p += 8, q += 8)
    Reverse_64(q, p);
  intern_src = p;
#else
  unsigned char * p, * q;
  for (p = intern_src, q = data; len > 0; len--, p += 8, q += 8)
    Permute_64(q, ARCH_FLOAT_ENDIANNESS, p, 0x01234567);
  intern_src = p;
#endif
}

CAMLexport void caml_deserialize_error_r(CAML_R, char * msg)
{
  intern_cleanup_r(ctx);
  caml_failwith_r(ctx,msg);
}
