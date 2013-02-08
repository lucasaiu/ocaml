(* Luca Saiu, REENTRANTRUNTIME *)

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

(* FIXME: use a custom type instead *)
type t =
  int (*whatever*)

external cpu_no : unit -> int = "caml_cpu_no_r" "reentrant"

external self : unit -> t = "caml_context_self_r" "reentrant"
external is_main : t -> bool = "caml_context_is_main_r" "reentrant"
(* external is_alive : t -> bool = "caml_context_is_alive_r" "reentrant" *)

external split_into_array : int -> (int -> unit) -> (t array) = "caml_context_split_r" "reentrant"

let split_into_contexts how_many f =
  Array.to_list (split_into_array how_many f)

let split_into_context thunk =
  List.hd (split_into_contexts 1 (fun i -> thunk ()))


let to_string context =
  string_of_int ((Obj.magic context) :> int)

let sself () = to_string (self ())

external globals : unit -> 'a = "caml_global_array_r" "reentrant"
(* external globals_and_datum : 'a -> ('b * 'a) = "caml_global_tuple_and_datum_r" "reentrant" *)

let rec global_index_from global globals from =
  if globals.(from) == global then
    from
  else
    global_index_from global globals (from + 1);;
let global_index global =
  global_index_from global (globals ()) 0;;

 (* FIXME: fix the multi-thread case *)
external join_context : t -> unit = "caml_context_join_r" "reentrant"

let join_contexts contexts =
  List.iter join_context contexts


(* FIXME: use a custom type instead *)
type mailbox = int (*whatever*)
(* exception ForeignMailbox of mailbox *)

external make_mailbox : unit -> mailbox = "caml_camlprim_make_mailbox_r" "reentrant"

external send : mailbox -> 'a -> unit = "caml_context_send_r" "reentrant"

external receive : mailbox -> 'a = "caml_context_receive_r" "reentrant"

external context_of_mailbox : mailbox -> 'a = "caml_camlprim_context_of_mailbox_r" "reentrant"

let is_mailbox_local mailbox =
  (context_of_mailbox mailbox) = (self ())

let join1 mailbox =
  join_context (context_of_mailbox mailbox)

let join mailboxes =
  List.iter join1 mailboxes

let split context_no f =
  let split_mailbox_receiving_mailbox =
    make_mailbox () in
  let _ =
    split_into_contexts
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

let split1 f =
  List.hd (split 1 (fun _ mailbox -> f mailbox))
