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

(* Description of primitive functions *)

open Misc

type description =
  { prim_name: string;         (* Name of primitive  or C function *)
    prim_arity: int;           (* Number of arguments *)
    prim_ctx : bool;
    prim_alloc: bool;          (* Does it allocates or raise? *)
    prim_native_name: string;  (* Name of C function for the nat. code gen. *)
    prim_native_float: bool }  (* Does the above operate on unboxed floats? *)

let parse_declaration arity decl =
  match decl with
    | name :: decl ->
      let rec iter prim decl =
	match decl with
	    [] -> prim
	  | name2 :: "float" :: decl when prim.prim_native_name = "" ->
	    iter { prim with prim_native_name = name2; prim_native_float = true; } decl
	  | "noalloc" :: decl ->
	    iter { prim with prim_alloc = false } decl
	  | "reentrant" :: decl ->
	    iter { prim with prim_ctx = true } decl
	  | name2 :: decl when prim.prim_native_name = "" ->
	    iter { prim with prim_native_name = name2 } decl
	  | name :: _ ->
	    fatal_error (Printf.sprintf "Error in declaration of primitive \"%s\": unexpected annot \"%s\""
			   prim.prim_name name)
      in
      iter
	{prim_name = name; prim_arity = arity; prim_alloc = true; prim_ctx = false;
	 prim_native_name = ""; prim_native_float = false; } decl
    | [] ->
      fatal_error "Primitive.parse_declaration"


let description_list p =
  let list = [p.prim_name] in
  let list = if not p.prim_alloc then "noalloc" :: list else list in
  let list = if p.prim_ctx then "reentrant" :: list else list in
  let list =
    if p.prim_native_name <> "" then p.prim_native_name :: list else list
  in
  let list = if p.prim_native_float then "float" :: list else list in
  List.rev list

let native_name p =
  if p.prim_native_name <> ""
  then p.prim_native_name
  else p.prim_name

let byte_name p =
  p.prim_name
