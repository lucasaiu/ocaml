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

#define CAML_CONTEXT_ROOTS

/* Buffered input/output. */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include "config.h"
#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#include "alloc.h"
#include "custom.h"
#include "fail.h"
#include "io.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "signals.h"
#include "sys.h"
#include "intext.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* // FIXME: HORRIBLE KLUDGE --Luca Saiu REENTRANTRUNTIME */
/* #define fprintf(...) {} */
/* #define fflush(...) {} */

/* Hooks for locking channels */

/// Ugly and experimental: BEGIN --Luca Saiu REENTRANTRUNTIME
static void caml_my_lock_channel(struct channel *currently_unused){
  //fprintf(stderr, "io.c: [+] Locking %p\n", currently_unused);
  caml_acquire_global_lock();
}
static void caml_my_unlock_channel(struct channel *currently_unused){
  //fprintf(stderr, "io.c: [-] UNlocking %p\n", currently_unused);
  caml_release_global_lock();
}
/// Ugly and experimental: END --Luca Saiu REENTRANTRUNTIME


CAMLexport void (*caml_channel_mutex_free) (struct channel *) = NULL;
CAMLexport void (*caml_channel_mutex_lock) (struct channel *) = caml_my_lock_channel;//NULL;
CAMLexport void (*caml_channel_mutex_unlock) (struct channel *) = caml_my_unlock_channel;//NULL;
CAMLexport void (*caml_channel_mutex_unlock_exn) (void) = NULL;

/* List of opened channels.  Access to this global structure has to be
   protected via the global lock. */
CAMLexport struct channel * caml_all_opened_channels = NULL;

/* Basic functions over type struct channel *.
   These functions can be called directly from C.
   No locking is performed. */

/* Functions shared between input and output */

CAMLexport struct channel * caml_open_descriptor_in_r(CAML_R, int fd)
{
  struct channel * channel;

  channel = (struct channel *) caml_stat_alloc(sizeof(struct channel));
  channel->fd = fd;
  caml_enter_blocking_section_r(ctx);
  channel->offset = lseek(fd, 0, SEEK_CUR);
  caml_leave_blocking_section_r(ctx);
  channel->curr = channel->max = channel->buff;
  channel->end = channel->buff + IO_BUFFER_SIZE;
  channel->mutex = NULL;
  channel->revealed = 0;
  channel->old_revealed = 0;
  channel->refcount = 0;
  channel->flags = 0;
caml_acquire_global_lock();
  channel->next = caml_all_opened_channels;
  channel->prev = NULL;
  //channel->already_closed = 0;
  if (caml_all_opened_channels != NULL)
    caml_all_opened_channels->prev = channel;
  caml_all_opened_channels = channel;
caml_release_global_lock();
  return channel;
}

CAMLexport struct channel * caml_open_descriptor_out_r(CAML_R, int fd)
{
  struct channel * channel;

  channel = caml_open_descriptor_in_r(ctx, fd);
  channel->max = NULL;
  return channel;
}

static void unlink_channel(struct channel *channel)
{
caml_acquire_global_lock();
  if (channel->prev == NULL) {
    Assert (channel == caml_all_opened_channels);
    caml_all_opened_channels = caml_all_opened_channels->next;
    if (caml_all_opened_channels != NULL)
      caml_all_opened_channels->prev = NULL;
  } else {
    channel->prev->next = channel->next;
    if (channel->next != NULL) channel->next->prev = channel->prev;
  }
caml_release_global_lock();
}

CAMLexport void caml_close_channel(struct channel *channel)
{
  INIT_CAML_R;
  int greater_than_zero;
  close(channel->fd);
  Lock(channel);
  greater_than_zero = channel->refcount > 0;
  if((channel->fd >= 0) && (channel->fd < 3))
    DUMP("closing the channel with struct channel* %p, fd %i: its refcount is now %i\n", channel, channel->fd, (int)channel->refcount);
  //channel->already_closed = 1;
  Unlock(channel);
  if (greater_than_zero)
    return;
  if (caml_channel_mutex_free != NULL) (*caml_channel_mutex_free)(channel);
  unlink_channel(channel);
  caml_stat_free(channel);
}

CAMLexport file_offset caml_channel_size_r(CAML_R, struct channel *channel)
{
  file_offset offset;
  file_offset end;
  int fd;

  /* We extract data from [channel] before dropping the OCaml lock, in case
     someone else touches the block. */
  fd = channel->fd;
  offset = channel->offset;
  caml_enter_blocking_section_r(ctx);
  end = lseek(fd, 0, SEEK_END);
  if (end == -1 || lseek(fd, offset, SEEK_SET) != offset) {
    caml_leave_blocking_section_r(ctx);
    caml_sys_error_r(ctx, NO_ARG);
  }
  caml_leave_blocking_section_r(ctx);
  return end;
}

CAMLexport int caml_channel_binary_mode(struct channel *channel)
{
#if defined(_WIN32) || defined(__CYGWIN__)
  int oldmode = setmode(channel->fd, O_BINARY);
  if (oldmode == O_TEXT) setmode(channel->fd, O_TEXT);
  return oldmode == O_BINARY;
#else
  return 1;
#endif
}

/* Output */

#ifndef EINTR
#define EINTR (-1)
#endif
#ifndef EAGAIN
#define EAGAIN (-1)
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK (-1)
#endif

static int do_write_r(CAML_R, int fd, char *p, int n)
{
  int retcode;

again:
  caml_enter_blocking_section_r(ctx);
  retcode = write(fd, p, n);
  caml_leave_blocking_section_r(ctx);
  if (retcode == -1) {
    if (errno == EINTR) goto again;
    if ((errno == EAGAIN || errno == EWOULDBLOCK) && n > 1) {
      /* We couldn't do a partial write here, probably because
         n <= PIPE_BUF and POSIX says that writes of less than
         PIPE_BUF characters must be atomic.
         We first try again with a partial write of 1 character.
         If that fails too, we'll raise Sys_blocked_io below. */
      n = 1; goto again;
    }
  }
  if (retcode == -1) caml_sys_io_error_r(ctx, NO_ARG);
  return retcode;
}

/* Attempt to flush the buffer. This will make room in the buffer for
   at least one character. Returns true if the buffer is empty at the
   end of the flush, or false if some data remains in the buffer.
 */


CAMLexport int caml_flush_partial_r(CAML_R, struct channel *channel)
{
  int towrite, written;

/*   Lock(channel); */
/*   if(channel->already_closed){ */
/*     Unlock(channel); */
/* caml_release_global_lock(); */
/*     return 1; */
/*   } */
/*   Unlock(channel); */
  towrite = channel->curr - channel->buff;
  if (towrite > 0) {
    written = do_write_r(ctx, channel->fd, channel->buff, towrite);
    channel->offset += written;
    if (written < towrite)
      memmove(channel->buff, channel->buff + written, towrite - written);
    channel->curr -= written;
  }
  return (channel->curr == channel->buff);
}

/* Flush completely the buffer. */

CAMLexport void caml_flush_r(CAML_R, struct channel *channel)
{
  /* Lock(channel); */
  /* if(channel->already_closed){ */
  /*   Unlock(channel); */
  /*   return; */
  /* } */
  /* Unlock(channel); */
  while (! caml_flush_partial_r(ctx, channel)) /*nothing*/;
}

/* Output data */

CAMLexport void caml_putword_r(CAML_R, struct channel *channel, uint32 w)
{
  if (! caml_channel_binary_mode(channel))
    caml_failwith_r(ctx, "output_binary_int: not a binary channel");
  putch(channel, w >> 24);
  putch(channel, w >> 16);
  putch(channel, w >> 8);
  putch(channel, w);
}

CAMLexport int caml_putblock_r(CAML_R, struct channel *channel, char *p, intnat len)
{
  int n, free, towrite, written;

  n = len >= INT_MAX ? INT_MAX : (int) len;
  free = channel->end - channel->curr;
  if (n < free) {
    /* Write request small enough to fit in buffer: transfer to buffer. */
    memmove(channel->curr, p, n);
    channel->curr += n;
    return n;
  } else {
    /* Write request overflows buffer (or just fills it up): transfer whatever
       fits to buffer and write the buffer */
    memmove(channel->curr, p, free);
    towrite = channel->end - channel->buff;
    written = do_write_r(ctx, channel->fd, channel->buff, towrite);
    if (written < towrite)
      memmove(channel->buff, channel->buff + written, towrite - written);
    channel->offset += written;
    channel->curr = channel->end - written;
    return free;
  }
}

CAMLexport void caml_really_putblock_r(CAML_R, struct channel *channel,
                                     char *p, intnat len)
{
  int written;
  while (len > 0) {
    written = caml_putblock_r(ctx, channel, p, len);
    p += written;
    len -= written;
  }
}

CAMLexport void caml_seek_out_r(CAML_R, struct channel *channel, file_offset dest)
{
  caml_flush_r(ctx, channel);
  caml_enter_blocking_section_r(ctx);
  if (lseek(channel->fd, dest, SEEK_SET) != dest) {
    caml_leave_blocking_section_r(ctx);
    caml_sys_error_r(ctx, NO_ARG);
  }
  caml_leave_blocking_section_r(ctx);
  channel->offset = dest;
}

CAMLexport file_offset caml_pos_out(struct channel *channel)
{
  return channel->offset + (file_offset)(channel->curr - channel->buff);
}

/* Input */

/* caml_do_read_r is exported for Cash */
CAMLexport int caml_do_read_r(CAML_R, int fd, char *p, unsigned int n)
{
  int retcode;

  do {
    caml_enter_blocking_section_r(ctx);
    retcode = read(fd, p, n);
#if defined(_WIN32)
    if (retcode == -1 && errno == ENOMEM && n > 16384){
      retcode = read(fd, p, 16384);
    }
#endif
    caml_leave_blocking_section_r(ctx);
  } while (retcode == -1 && errno == EINTR);
  if (retcode == -1) caml_sys_io_error_r(ctx, NO_ARG);
  return retcode;
}

CAMLexport unsigned char caml_refill_r(CAML_R, struct channel *channel)
{
  int n;

  n = caml_do_read_r(ctx, channel->fd, channel->buff, channel->end - channel->buff);
  if (n == 0) caml_raise_end_of_file_r(ctx);
  channel->offset += n;
  channel->max = channel->buff + n;
  channel->curr = channel->buff + 1;
  return (unsigned char)(channel->buff[0]);
}

CAMLexport uint32 caml_getword_r(CAML_R, struct channel *channel)
{
  int i;
  uint32 res;

  if (! caml_channel_binary_mode(channel))
    caml_failwith_r(ctx, "input_binary_int: not a binary channel");
  res = 0;
  for(i = 0; i < 4; i++) {
    res = (res << 8) + getch(channel);
  }
  return res;
}

CAMLexport int caml_getblock_r(CAML_R, struct channel *channel, char *p, intnat len)
{
  int n, avail, nread;

  n = len >= INT_MAX ? INT_MAX : (int) len;
  avail = channel->max - channel->curr;
  if (n <= avail) {
    memmove(p, channel->curr, n);
    channel->curr += n;
    return n;
  } else if (avail > 0) {
    memmove(p, channel->curr, avail);
    channel->curr += avail;
    return avail;
  } else {
    nread = caml_do_read_r(ctx, channel->fd, channel->buff,
                         channel->end - channel->buff);
    channel->offset += nread;
    channel->max = channel->buff + nread;
    if (n > nread) n = nread;
    memmove(p, channel->buff, n);
    channel->curr = channel->buff + n;
    return n;
  }
}

CAMLexport int caml_really_getblock_r(CAML_R, struct channel *chan, char *p, intnat n)
{
  int r;
  while (n > 0) {
    r = caml_getblock_r(ctx, chan, p, n);
    if (r == 0) break;
    p += r;
    n -= r;
  }
  return (n == 0);
}

CAMLexport void caml_seek_in_r(CAML_R, struct channel *channel, file_offset dest)
{
  if (dest >= channel->offset - (channel->max - channel->buff) &&
      dest <= channel->offset) {
    channel->curr = channel->max - (channel->offset - dest);
  } else {
    caml_enter_blocking_section_r(ctx);
    if (lseek(channel->fd, dest, SEEK_SET) != dest) {
      caml_leave_blocking_section_r(ctx);
      caml_sys_error_r(ctx, NO_ARG);
    }
    caml_leave_blocking_section_r(ctx);
    channel->offset = dest;
    channel->curr = channel->max = channel->buff;
  }
}

CAMLexport file_offset caml_pos_in(struct channel *channel)
{
  return channel->offset - (file_offset)(channel->max - channel->curr);
}

CAMLexport intnat caml_input_scan_line_r(CAML_R, struct channel *channel)
{
  char * p;
  int n;

  p = channel->curr;
  do {
    if (p >= channel->max) {
      /* No more characters available in the buffer */
      if (channel->curr > channel->buff) {
        /* Try to make some room in the buffer by shifting the unread
           portion at the beginning */
        memmove(channel->buff, channel->curr, channel->max - channel->curr);
        n = channel->curr - channel->buff;
        channel->curr -= n;
        channel->max -= n;
        p -= n;
      }
      if (channel->max >= channel->end) {
        /* Buffer is full, no room to read more characters from the input.
           Return the number of characters in the buffer, with negative
           sign to indicate that no newline was encountered. */
        return -(channel->max - channel->curr);
      }
      /* Fill the buffer as much as possible */
      n = caml_do_read_r(ctx, channel->fd, channel->max, channel->end - channel->max);
      if (n == 0) {
        /* End-of-file encountered. Return the number of characters in the
           buffer, with negative sign since we haven't encountered
           a newline. */
        return -(channel->max - channel->curr);
      }
      channel->offset += n;
      channel->max += n;
    }
  } while (*p++ != '\n');
  /* Found a newline. Return the length of the line, newline included. */
  return (p - channel->curr);
}

/* OCaml entry points for the I/O functions.  Wrap struct channel *
   objects into a heap-allocated object.  Perform locking
   and unlocking around the I/O operations. */
/* FIXME CAMLexport, but not in io.h  exported for Cash ? */
CAMLexport void caml_finalize_channel(value vchan)
{
  struct channel * chan = Channel(vchan);
  int greater_than_zero;
  INIT_CAML_R;
  Lock(chan);
  greater_than_zero = --chan->refcount > 0;
  QDUMP("finalizing the channel with struct channel* %p, fd %i: its refcount is now %i", chan, chan->fd, chan->refcount);
  Unlock(chan);
  if (greater_than_zero)
    return;
  QDUMP("destroying the channel with struct channel* %p, fd %i", chan, chan->fd);
  if (caml_channel_mutex_free != NULL) (*caml_channel_mutex_free)(chan);
  unlink_channel(chan);
  caml_stat_free(chan);
}

static int compare_channel(value vchan1, value vchan2)
{
  struct channel * chan1 = Channel(vchan1);
  struct channel * chan2 = Channel(vchan2);
  return (chan1 == chan2) ? 0 : (chan1 < chan2) ? -1 : 1;
}

static intnat hash_channel(value vchan)
{
  return (intnat) (Channel(vchan));
}

/* The wire format is trivial: just one word, the shared struct
   channel pointer.  We don't need to care about endianness. */
static void cross_context_serialize_channel(value v,
                                            /*out*/ uintnat * wsize_32 /*size in bytes*/,
                                            /*out*/ uintnat * wsize_64 /*size in bytes*/){
  INIT_CAML_R;
  CAMLparam1(v);
  /* The channel is used by one more context.  Pin it: */
  struct channel *pointer = Channel(v);

  /* The data part is just the struct channel* pointer. */
  *wsize_32 = 4;
  *wsize_64 = 8;
#ifdef ARCH_SIXTYFOUR
  //fprintf(stderr, "Serializing the 8-byte integer %li\n", (long)pointer); fflush(stderr);
  caml_serialize_int_8_r(ctx, (int64)pointer);
#else
  //fprintf(stderr, "Serialized the 4-byte integer %li\n", (long)pointer); fflush(stderr);
  caml_serialize_int_4_r(ctx, (int32)pointer);
#endif // #else // #ifdef ARCH_SIXTYFOUR
  //fprintf(stderr, "Serializing the channel at %p, fd %i: still alive at the end\n", pointer, pointer->fd); fflush(stderr);
  CAMLreturn0;
}
static uintnat cross_context_deserialize_channel(void * dst){
  //fprintf(stderr, "Deserializing a channel: this is the very beginning\n"); fflush(stderr);
  INIT_CAML_R;
  /* Deserialize the pointer: */
  struct channel *pointer =
#ifdef ARCH_SIXTYFOUR
    (void*)caml_deserialize_uint_8_r(ctx);
#else
    (void*)caml_deserialize_uint_4_r(ctx);
#endif // #else // #ifdef ARCH_SIXTYFOUR
  //fprintf(stderr, "Deserializing the channel at %p, fd %i\n", pointer, pointer->fd); fflush(stderr);

  /* Copy the pointer into the custom object payload as its only
     word, and pin it: */
  *((struct channel**)dst) = pointer;
  Lock(pointer);
  pointer->refcount ++;
  QDUMP("Cross-context-deserializing the channel with struct channel* %p, fd %i: its refcount is now %i", pointer, pointer->fd, pointer->refcount);
  Unlock(pointer);

  //fprintf(stderr, "Deserializing the channel at %p, fd %i: still alive at the end\n", pointer, pointer->fd); fflush(stderr);
  //fprintf(stderr, "cross_context_deserialize_channel: FIXME: implement\n"); fflush(stderr);
  /* The payload is one word: */
  return sizeof(void*);
}

struct custom_operations caml_channel_operations = {
  "_chan",
  caml_finalize_channel,
  compare_channel,
  hash_channel,
  custom_serialize_default,
  cross_context_deserialize_channel,//custom_deserialize_default,
  custom_compare_ext_default,
  cross_context_serialize_channel,
  cross_context_deserialize_channel // FIXME: I probably don't want this as a further field
};

CAMLexport value caml_alloc_channel_r(CAML_R, struct channel *chan)
{
  value res;
  Lock(chan);
  chan->refcount++;             /* prevent finalization during next alloc */
  QDUMP("allocating a channel with struct channel* %p, fd %i: its refcount is now %i", chan, chan->fd, chan->refcount);
  Unlock(chan);

  res = caml_alloc_custom(&caml_channel_operations, sizeof(struct channel *),
                          1, 1000);
  Channel(res) = chan;
  return res;
}

CAMLprim value caml_ml_open_descriptor_in_r(CAML_R, value fd)
{
  return caml_alloc_channel_r(ctx, caml_open_descriptor_in_r(ctx, Int_val(fd)));
}

CAMLprim value caml_ml_open_descriptor_out_r(CAML_R, value fd)
{
  return caml_alloc_channel_r(ctx, caml_open_descriptor_out_r(ctx,Int_val(fd)));
}

static value caml_ml_channels_list_r (CAML_R, const int output_only)
{
  CAMLparam0 ();
  CAMLlocal3 (res, tail, chan);
  struct channel * channel;

  res = Val_emptylist;
caml_acquire_global_lock();
  for (channel = caml_all_opened_channels;
       channel != NULL;
       channel = channel->next)
    /* Testing channel->fd >= 0 looks unnecessary, as
       caml_ml_close_channel changes max when setting fd to -1. */
    if ((channel->max == NULL) || ! output_only) {
      chan = caml_alloc_channel_r (ctx, channel);
      tail = res;
      res = caml_alloc_small_r (ctx, 2, 0);
      Field (res, 0) = chan;
      Field (res, 1) = tail;
    }
caml_release_global_lock();
  CAMLreturn (res);
}

value caml_ml_all_channels_list_r (CAML_R)
{
  return caml_ml_channels_list_r(ctx, 0);
}

CAMLprim value caml_ml_out_channels_list_r (CAML_R, value unit)
{
  return caml_ml_channels_list_r(ctx, 1);
}

CAMLprim value caml_channel_descriptor_r(CAML_R, value vchannel)
{
  int fd = Channel(vchannel)->fd;
  if (fd == -1) { errno = EBADF; caml_sys_error_r(ctx, NO_ARG); }
  return Val_int(fd);
}

CAMLprim value caml_ml_close_channel_r(CAML_R, value vchannel)
{
  int result;
  int do_syscall;
  int fd;

  /* For output channels, must have flushed before */
  struct channel * channel = Channel(vchannel);
  if (channel->fd != -1){
    fd = channel->fd;
    QDUMP("closing a channel with struct channel* %p, fd %i [now -1]: its refcount is %i", channel, channel->fd, channel->refcount);
    channel->fd = -1;
    do_syscall = 1;
  }else{
    do_syscall = 0;
    result = 0;
  }
  /* Ensure that every read or write on the channel will cause an
     immediate caml_flush_partial or caml_refill, thus raising a Sys_error
     exception */
  channel->curr = channel->max = channel->end;

  if (do_syscall) {
    caml_enter_blocking_section_r(ctx);
    result = close(fd);
    caml_leave_blocking_section_r(ctx);
  }

  if (result == -1) caml_sys_error_r (ctx,NO_ARG);
  return Val_unit;
}

/* EOVERFLOW is the Unix98 error indicating that a file position or file
   size is not representable.
   ERANGE is the ANSI C error indicating that some argument to some
   function is out of range.  This is less precise than EOVERFLOW,
   but guaranteed to be defined on all ANSI C environments. */
#ifndef EOVERFLOW
#define EOVERFLOW ERANGE
#endif

CAMLprim value caml_ml_channel_size_r(CAML_R, value vchannel)
{
  file_offset size = caml_channel_size_r(ctx, Channel(vchannel));
  if (size > Max_long) { errno = EOVERFLOW; caml_sys_error_r(ctx, NO_ARG); }
  return Val_long(size);
}

CAMLprim value caml_ml_channel_size_64_r(CAML_R, value vchannel)
{
  return Val_file_offset(caml_channel_size_r(ctx, Channel(vchannel)));
}

CAMLprim value caml_ml_set_binary_mode(value vchannel, value mode)
{
#if defined(_WIN32) || defined(__CYGWIN__)
  struct channel * channel = Channel(vchannel);
  if (setmode(channel->fd, Bool_val(mode) ? O_BINARY : O_TEXT) == -1)
    caml_sys_error(NO_ARG);
#endif
  return Val_unit;
}

/*
   If the channel is closed, DO NOT raise a "bad file descriptor"
   exception, but do nothing (the buffer is already empty).
   This is because some libraries will flush at exit, even on
   file descriptors that may be closed.
*/

CAMLprim value caml_ml_flush_partial_r(CAML_R, value vchannel)
{
  CAMLparam1 (vchannel);
  struct channel * channel = Channel(vchannel);
  int res;

  if (channel->fd == -1) CAMLreturn(Val_true);
  Lock(channel);
  res = caml_flush_partial_r(ctx, channel);
  Unlock(channel);
  CAMLreturn (Val_bool(res));
}

CAMLprim value caml_ml_flush_r(CAML_R, value vchannel)
{
  CAMLparam1 (vchannel);
  struct channel * channel = Channel(vchannel);

  if (channel->fd == -1) CAMLreturn(Val_unit);
  //fprintf(stderr, "caml_ml_flush_r: OK1 from thread %p\n", pthread_self()); fflush(stderr);
  Lock(channel);
  //fprintf(stderr, "caml_ml_flush_r: OK2 from thread %p\n", pthread_self()); fflush(stderr);
  caml_flush_r(ctx, channel);
  //fprintf(stderr, "caml_ml_flush_r: OK3 from thread %p\n", pthread_self()); fflush(stderr);
  Unlock(channel);
  //fprintf(stderr, "caml_ml_flush_r: OK4 from thread %p\n", pthread_self()); fflush(stderr);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_output_char_r(CAML_R, value vchannel, value ch)
{
  CAMLparam2 (vchannel, ch);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  putch(channel, Long_val(ch));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_output_int_r(CAML_R, value vchannel, value w)
{
  CAMLparam2 (vchannel, w);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  caml_putword_r(ctx, channel, Long_val(w));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_output_partial_r(CAML_R, value vchannel, value buff, value start,
                                      value length)
{
  CAMLparam4 (vchannel, buff, start, length);
  struct channel * channel = Channel(vchannel);
  int res;

  Lock(channel);
  res = caml_putblock_r(ctx, channel, &Byte(buff, Long_val(start)), Long_val(length));
  Unlock(channel);
  CAMLreturn (Val_int(res));
}

CAMLprim value caml_ml_output_r(CAML_R, value vchannel, value buff, value start,
                              value length)
{
  CAMLparam4 (vchannel, buff, start, length);
  struct channel * channel = Channel(vchannel);
  intnat pos = Long_val(start);
  intnat len = Long_val(length);

  Lock(channel);
    while (len > 0) {
      int written = caml_putblock_r(ctx, channel, &Byte(buff, pos), len);
      pos += written;
      len -= written;
    }
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_seek_out_r(CAML_R, value vchannel, value pos)
{
  CAMLparam2 (vchannel, pos);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  caml_seek_out_r(ctx, channel, Long_val(pos));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_seek_out_64_r(CAML_R, value vchannel, value pos)
{
  CAMLparam2 (vchannel, pos);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  caml_seek_out_r(ctx,channel, File_offset_val(pos));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_pos_out_r(CAML_R, value vchannel)
{
  file_offset pos = caml_pos_out(Channel(vchannel));
  if (pos > Max_long) { errno = EOVERFLOW; caml_sys_error_r(ctx, NO_ARG); }
  return Val_long(pos);
}

CAMLprim value caml_ml_pos_out_64_r(CAML_R, value vchannel)
{
  return Val_file_offset(caml_pos_out(Channel(vchannel)));
}

CAMLprim value caml_ml_input_char_r(CAML_R, value vchannel)
{
  CAMLparam1 (vchannel);
  struct channel * channel = Channel(vchannel);
  unsigned char c;

  Lock(channel);
  c = getch(channel);
  Unlock(channel);
  CAMLreturn (Val_long(c));
}

CAMLprim value caml_ml_input_int_r(CAML_R, value vchannel)
{
  CAMLparam1 (vchannel);
  struct channel * channel = Channel(vchannel);
  intnat i;

  Lock(channel);
  i = caml_getword_r(ctx, channel);
  Unlock(channel);
#ifdef ARCH_SIXTYFOUR
  i = (i << 32) >> 32;          /* Force sign extension */
#endif
  CAMLreturn (Val_long(i));
}

CAMLprim value caml_ml_input_r(CAML_R, value vchannel, value buff, value vstart,
                               value vlength)
{
  CAMLparam4 (vchannel, buff, vstart, vlength);
  struct channel * channel = Channel(vchannel);
  intnat start, len;
  int n, avail, nread;

  Lock(channel);
  /* We cannot call caml_getblock here because buff may move during
     caml_do_read */
  start = Long_val(vstart);
  len = Long_val(vlength);
  n = len >= INT_MAX ? INT_MAX : (int) len;
  avail = channel->max - channel->curr;
  if (n <= avail) {
    memmove(&Byte(buff, start), channel->curr, n);
    channel->curr += n;
  } else if (avail > 0) {
    memmove(&Byte(buff, start), channel->curr, avail);
    channel->curr += avail;
    n = avail;
  } else {
    nread = caml_do_read_r(ctx, channel->fd, channel->buff,
                         channel->end - channel->buff);
    channel->offset += nread;
    channel->max = channel->buff + nread;
    if (n > nread) n = nread;
    memmove(&Byte(buff, start), channel->buff, n);
    channel->curr = channel->buff + n;
  }
  Unlock(channel);
  CAMLreturn (Val_long(n));
}

CAMLprim value caml_ml_seek_in_r(CAML_R, value vchannel, value pos)
{
  CAMLparam2 (vchannel, pos);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  caml_seek_in_r(ctx,channel, Long_val(pos));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_seek_in_64_r(CAML_R, value vchannel, value pos)
{
  CAMLparam2 (vchannel, pos);
  struct channel * channel = Channel(vchannel);

  Lock(channel);
  caml_seek_in_r(ctx, channel, File_offset_val(pos));
  Unlock(channel);
  CAMLreturn (Val_unit);
}

CAMLprim value caml_ml_pos_in_r(CAML_R, value vchannel)
{
  file_offset pos = caml_pos_in(Channel(vchannel));
  if (pos > Max_long) { errno = EOVERFLOW; caml_sys_error_r(ctx, NO_ARG); }
  return Val_long(pos);
}

CAMLprim value caml_ml_pos_in_64_r(CAML_R, value vchannel)
{
  return Val_file_offset(caml_pos_in(Channel(vchannel)));
}

CAMLprim value caml_ml_input_scan_line_r(CAML_R, value vchannel)
{
  CAMLparam1 (vchannel);
  struct channel * channel = Channel(vchannel);
  intnat res;

  Lock(channel);
  res = caml_input_scan_line_r(ctx, channel);
  Unlock(channel);
  CAMLreturn (Val_long(res));
}

/* Conversion between file_offset and int64 */

#ifndef ARCH_INT64_TYPE
CAMLexport value caml_Val_file_offset(file_offset fofs)
{
  int64 ofs;
  ofs.l = fofs;
  ofs.h = 0;
  return caml_copy_int64(ofs);
}

CAMLexport file_offset caml_File_offset_val(value v)
{
  int64 ofs = Int64_val(v);
  return (file_offset) ofs.l;
}
#endif
