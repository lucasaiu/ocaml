(***********************************************************************)
(*                                                                     *)
(*                                OCaml                                *)
(*                                                                     *)
(*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         *)
(*                                                                     *)
(*  Copyright 1996 Institut National de Recherche en Informatique et   *)
(*  en Automatique.  All rights reserved.  This file is distributed    *)
(*  under the terms of the GNU Library General Public License, with    *)
(*  the special exception on linking described in file ../LICENSE.     *)
(*                                                                     *)
(***********************************************************************)

(* $Id$ *)

(* Ensure that [at_exit] functions are called at the end of every program *)

(* let _ = do_at_exit () *)

(* let _ = Printf.fprintf stderr "std_exit: ABOUT TO (MAYBE) CALL do_at_exit\n" let _ = flush stderr *)
(* FIXME: For the time being I've decided that do_at_exit is only
   called when the main context terminates.  Do we want this?  Do we
   want something more general, with a *different* function called in
   non-main contexts?  --Luca Saiu REENTRANTRUNTIME *)
let _ =
  if Context.is_main (Context.self ()) then
    do_at_exit ()
  else
    ()
