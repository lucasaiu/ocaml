(*************************************************************************)
(*                                                                       *)
(*                Objective Caml LablTk library                          *)
(*                                                                       *)
(*         Francois Rouaix, Francois Pessaux and Jun Furuse              *)
(*               projet Cristal, INRIA Rocquencourt                      *)
(*            Jacques Garrigue, Kyoto University RIMS                    *)
(*                                                                       *)
(*   Copyright 1999 Institut National de Recherche en Informatique et    *)
(*   en Automatique and Kyoto University.  All rights reserved.          *)
(*   This file is distributed under the terms of the GNU Library         *)
(*   General Public License.                                             *)
(*                                                                       *)
(*************************************************************************)

(* $Id$ *)

(* Support for Tk -textvariable option *)
open Widget
open Protocol

type textVariable
      (* TextVariable is an abstract type *)

val create : ?on: 'a widget -> unit -> textVariable
      (* Allocation of a textVariable with lifetime associated to widget 
         if a widget is specified *)
val set : textVariable -> string -> unit
      (* Setting the val of a textVariable *)
val get : textVariable -> string
      (* Reading the val of a textVariable *)
val name : textVariable -> string
      (* Its tcl name *)

val cCAMLtoTKtextVariable : textVariable -> tkArgs
      (* Internal conversion function *)

val handle : textVariable -> (unit -> unit) -> unit
      (* Callbacks on variable modifications *)

val coerce : string -> textVariable

(*-*)
val free : textVariable -> unit
