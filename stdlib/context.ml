(* Luca Saiu, REENTRANTRUNTIME *)

(* Return true iff multi-context support is implemented: *)
(* external implemented : unit -> bool = "caml_multi_context_implemented" *)
let implemented () = true

let implemented_bool = implemented ()

exception Unimplemented

(* Utility.  These should be moved to List, in an ideal world. *)
let range a b =
  let rec range_acc a b acc =
    if a > b then
      acc
    else
      range_acc a (b - 1) (b :: acc) in
  range_acc a b [];;

let iota n =
  range 0 (n - 1);;

exception CannotSplit
let _ = Callback.register_exception "Context.CannotSplit" CannotSplit
let _ = Callback.register_exception "Context.Unimplemented" Unimplemented

(* FIXME: use a custom type instead *)
type t =
  int (*whatever*)

external cpu_no : unit -> int = "caml_cpu_no_r" "reentrant"

external self : unit -> t = "caml_context_self_r" "reentrant"
external is_main : t -> bool = "caml_context_is_main_r" "reentrant"
(* external is_alive : t -> bool = "caml_context_is_alive_r" "reentrant" *)

external actually_split_into_context_array : int -> (int -> unit) -> (t array) = "caml_context_split_r" "reentrant"

let split_into_context_array =
  if implemented_bool then
    actually_split_into_context_array
  else
    raise Unimplemented

let split_into_context_list how_many f =
  Array.to_list (split_into_context_array how_many f)

let split_into_context thunk =
  List.hd (split_into_context_list 1 (fun _ -> thunk ()))

(* Debugging stuff *)

let to_string context =
  string_of_int ((Obj.magic context) :> int)

let sself () = to_string (self ())

external globals_function : unit -> 'a = "caml_global_array_r" "reentrant"

let globals = globals_function ()

let rec global_index_from global globals from =
  if globals.(from) = (* == *) global then
    from
  else
    global_index_from global globals (from + 1);;
let global_index global =
  global_index_from global globals 0;;

external actually_join_context : t -> unit = "caml_context_join_r" "reentrant"
let join_context =
  if implemented_bool then
    actually_join_context
  else
    raise Unimplemented

let join_contexts contexts =
  if implemented_bool then
    List.iter join_context contexts
  else
    raise Unimplemented


(* FIXME: use a custom type instead *)
type mailbox = int (*whatever*)
(* exception ForeignMailbox of mailbox *)

external actually_make_mailbox : unit -> mailbox = "caml_camlprim_make_mailbox_r" "reentrant"
let make_mailbox =
  if implemented_bool then
    actually_make_mailbox
  else
    raise Unimplemented

external actually_send : mailbox -> 'a -> unit = "caml_context_send_r" "reentrant"
let send mailbox message =
  if implemented_bool then
    actually_send mailbox message
  else
    raise Unimplemented

external actually_receive : mailbox -> 'a = "caml_context_receive_r" "reentrant"
let receive =
  if implemented_bool then
    actually_receive
  else
    raise Unimplemented

external actual_context_of_mailbox : mailbox -> 'a = "caml_camlprim_context_of_mailbox_r" "reentrant"
let context_of_mailbox =
  if implemented_bool then
    actual_context_of_mailbox
  else
    raise Unimplemented

let is_mailbox_local mailbox =
  (context_of_mailbox mailbox) = (self ())

let join1 mailbox =
  join_context (context_of_mailbox mailbox)

let join mailboxes =
  if implemented_bool then
    List.iter join1 mailboxes
  else
    raise Unimplemented

let split context_no f =
  let split_mailbox_receiving_mailbox =
    make_mailbox () in
  let _ =
    split_into_context_list
      context_no
      (fun index ->
        let mailbox = make_mailbox () in
        send split_mailbox_receiving_mailbox (index, mailbox);
        f index mailbox) in
  (* Printf.fprintf stderr "@@@@ split: made %i contexts\n%!" (List.length contexts); *)
  List.rev_map
    snd
    (List.sort
       (fun (index1, _) (index2, _) -> compare index2 index1)
       (List.map
          (fun _ -> receive split_mailbox_receiving_mailbox)
          (iota context_no)))

let split_into_array context_no f =
  Array.of_list (split context_no f)

let split1 f =
  List.hd (split 1 (fun _ mailbox -> f mailbox))

let at_context_exit_functions : (unit -> unit) list ref =
  ref []

(* This is UNSAFE and shouldn't be exposed to the user.  FIXME: is it really needed? *)
external unpin_this_context : unit -> unit = "caml_unpin_context_primitive_r" "reentrant"

(* FIXME: remove after debugging *)
external dump : string -> unit = "caml_dump_r" "reentrant"
(* FIXME: remove after debugging *)
external set_debugging : bool -> unit = "caml_set_debugging"

(* This is to be called from C: *)
let run_at_context_exit_functions () =
  (* dump "Executing \"contextual\" at_exit functions"; *)
  List.iter
    (fun f -> f ())
    ((* List.rev *) !at_context_exit_functions)(* ; *)
  (* dump "Executed \"contextual\" at_exit functions" *)
let () =
  Callback.register "Context.run_at_context_exit_functions" run_at_context_exit_functions

let at_exit f =
  at_context_exit_functions := f :: !at_context_exit_functions

(* let _ = *)
(*   at_exit *)
(*     (fun () -> *)
(*       dump "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE  about to exit the context") *)
