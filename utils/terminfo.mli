(***********************************************************************)
(*                                                                     *)
(*                                OCaml                                *)
(*                                                                     *)
(*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         *)
(*                                                                     *)
(*  Copyright 1996 Institut National de Recherche en Informatique et   *)
(*  en Automatique.  All rights reserved.  This file is distributed    *)
(*  under the terms of the Q Public License version 1.0.               *)
(*                                                                     *)
(***********************************************************************)

(* $Id$ *)

(* Basic interface to the terminfo database *)

type status =
  | Uninitialised
  | Bad_term
  | Good_term of int  (* number of lines of the terminal *)
;;
external setup : out_channel -> status = "caml_terminfo_setup_r" "reentrant";;
external backup : int -> unit = "caml_terminfo_backup_r" "reentrant";;
external standout : bool -> unit = "caml_terminfo_standout_r" "reentrant";;
external resume : int -> unit = "caml_terminfo_resume_r" "reentrant";;


(* (\* FIXME: compatibility hack; remove --Luca Saiu REENTRANTRUNTIME *\) *)
(* val setup : out_channel -> status;; *)
(* val backup : int -> unit;; *)
(* val standout : bool -> unit;; *)
(* val resume : int -> unit;; *)
