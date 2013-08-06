(***********************************************************************)
(*                                                                     *)
(*                                OCaml                                *)
(*                                                                     *)
(*  Xavier Leroy and Pascal Cuoq, projet Cristal, INRIA Rocquencourt   *)
(*                                                                     *)
(*  Copyright 1996 Institut National de Recherche en Informatique et   *)
(*  en Automatique.  All rights reserved.  This file is distributed    *)
(*  under the terms of the GNU Library General Public License, with    *)
(*  the special exception on linking described in file ../../LICENSE.  *)
(*                                                                     *)
(***********************************************************************)

(* $Id$ *)

(* User-level threads *)

type t

external thread_initialize : unit -> unit = "caml_thread_initialize_r" "reentrant"
external thread_cleanup : unit -> unit = "caml_thread_cleanup_r" "reentrant"
external thread_new : (unit -> unit) -> t = "caml_thread_new_r" "reentrant"
external thread_uncaught_exception : exn -> unit =
            "caml_thread_uncaught_exception_r" "reentrant"

external yield : unit -> unit = "caml_thread_yield_r" "reentrant"
external self : unit -> t = "caml_thread_self_r" "reentrant"
external id : t -> int = "caml_thread_id"
external join : t -> unit = "caml_thread_join_r" "reentrant"
external exit : unit -> unit = "caml_thread_exit_r" "reentrant"

(* For new, make sure the function passed to thread_new never
   raises an exception. *)

let create fn arg =
  thread_new
    (fun () ->
Printf.fprintf stderr "* About to start caml code in new thread: compacting...\n%!";
(* Gc.compact (); *)
Printf.fprintf stderr "* ...compacted.  Now really starting caml code in new thread\n%!";
      try
        fn arg; ()
      with exn ->
             flush stdout; flush stderr;
Printf.fprintf stderr "* EXITING thread created by Thread.create\n%!";
             thread_uncaught_exception exn)

(* Thread.kill is currently not implemented due to problems with
   cleanup handlers on several platforms *)

let kill th = invalid_arg "Thread.kill: not implemented"

(* Preemption *)

let preempt signal = yield()

(* Initialization of the scheduler *)

let preempt_signal =
  match Sys.os_type with
  | "Win32" -> Sys.sigterm
  | _       -> Sys.sigvtalrm

let _ =
Context.dump "* INITIALIZING systhreads\n%!";
  Sys.set_signal preempt_signal (Sys.Signal_handle preempt);
  (* Sys.set_signal preempt_signal (Sys.Signal_handle (fun s -> prerr_string "**************{got signal "; prerr_int s; prerr_string " [FIXME: call preempt instead]}\n"; flush stderr)); *)
  thread_initialize();
  Context.at_exit
    (fun () ->
Context.dump "Executing the Context.at_exit function registered by systhreads: killing the tick thread";
      thread_cleanup ();
      Sys.set_signal preempt_signal Sys.Signal_default;
Context.dump "Executed the Context.at_exit function registered by systhreads";
);
  (* I moved these to the Context.at_exit part --L.S. *)
  (* at_exit *)
  (*   (fun () -> *)
  (*       thread_cleanup(); *)
  (*       (\* In case of DLL-embedded OCaml the preempt_signal handler *)
  (*          will point to nowhere after DLL unloading and an accidental *)
  (*          preempt_signal will crash the main program. So restore the *)
  (*          default handler. *\) *)
  (*       Sys.set_signal preempt_signal Sys.Signal_default); *)
Context.dump "* INITIALIZED systhreads\n%!"


(* Wait functions *)

let delay time = ignore(Unix.select [] [] [] time)

let wait_read fd = ()
let wait_write fd = ()

let wait_timed_read fd d =
  match Unix.select [fd] [] [] d with ([], _, _) -> false | (_, _, _) -> true
let wait_timed_write fd d =
  match Unix.select [] [fd] [] d with (_, [], _) -> false | (_, _, _) -> true
let select = Unix.select

let wait_pid p = Unix.waitpid [] p

external sigmask : Unix.sigprocmask_command -> int list -> int list = "caml_thread_sigmask_r" "reentrant"
external wait_signal : int list -> int = "caml_wait_signal_r" "reentrant"
