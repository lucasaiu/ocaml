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

external join_context : t -> unit = "caml_context_join_r" "reentrant"

let join_contexts contexts =
  List.iter join_context contexts


(* FIXME: use a custom type instead *)
type mailbox = int (*whatever*)
(* exception ForeignMailbox of mailbox *)

external make_local_mailbox : unit -> mailbox = "caml_camlprim_make_local_mailbox_r" "reentrant"

external send : mailbox -> 'a -> unit = "caml_context_send_r" "reentrant"

external receive_ugly_exception : mailbox -> (t * 'a) = "caml_context_receive_r" "reentrant"
let receive mailbox =
  (* try *)
    let _, message = receive_ugly_exception mailbox in
    message
  (* with _ -> *)
  (*   raise (ForeignMailbox mailbox) *)

external context_of_mailbox : mailbox -> 'a = "caml_camlprim_context_of_mailbox_r" "reentrant"

let is_mailbox_local mailbox =
  (context_of_mailbox mailbox) = (self ())

let join1 mailbox =
  join_context (context_of_mailbox mailbox)

let join mailboxes =
  List.iter join1 mailboxes

let split context_no f =
  let split_mailbox_receiving_mailbox =
    make_local_mailbox () in
  let _ =
    split_into_contexts
      context_no
      (fun index ->
        let mailbox = make_local_mailbox () in
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


type 'a sink =   'a -> unit
type 'a source = unit -> 'a

(* let instantiate skeleton = *)
(*   skeleton () *)

let insert pair sorted_pairs =
  let rec insert_acc pair sorted_pairs reversed_part_to_prepend =
    let pair_index, pair_element = pair in
    match sorted_pairs with
    | [] ->
        List.rev_append reversed_part_to_prepend [pair]
    | ((first_index, _) as first_pair) :: rest ->
        if pair_index < first_index then
          List.rev_append reversed_part_to_prepend (pair :: sorted_pairs)
        else
          insert_acc pair rest (first_pair :: reversed_part_to_prepend) in
  insert_acc pair sorted_pairs []
let remove index sorted_pairs =
  let rec remove_acc index sorted_pairs reversed_part_to_prepend =
    match sorted_pairs with
    | [] ->
        List.rev reversed_part_to_prepend
    | ((first_index, _) as first_pair) :: rest ->
        if index <= first_index then
          List.rev_append reversed_part_to_prepend rest
        else
          remove_acc index rest (first_pair :: reversed_part_to_prepend) in
  remove_acc index sorted_pairs []
let rec is_in index sorted_pairs =
  match sorted_pairs with
  | [] ->
      false
  | (first, _) :: rest ->
      if first = index then
        true
      else if first > index then
        false
      else
        is_in index rest
let rec lookup index sorted_pairs =
  match sorted_pairs with
  | [] ->
      failwith "lookup"
  | (first, value) :: rest ->
      if first = index then
        value
      else if first > index then
        failwith "lookup"
      else
        lookup index rest
let reorder_source source required_minimum_index =
  let already_emitted = ref [] in
  let required_minimum_index = ref required_minimum_index in
  let rec result_source () =
    (* Printf.fprintf stderr "!already_emitted has length %i\n%!" (List.length !already_emitted); *)
    if is_in !required_minimum_index !already_emitted then
      let result = lookup !required_minimum_index !already_emitted in
      already_emitted := remove !required_minimum_index !already_emitted;
      required_minimum_index := !required_minimum_index + 1;
      result
    else
      let next_pair = source () in
      already_emitted := insert next_pair !already_emitted;
      result_source () in
  result_source

let make_reordering_sink sink first_index =
  let pending = ref [] in
  let required_minimum_index = ref first_index in
  let rec process_pending () =
    while is_in !required_minimum_index !pending do
      let element = lookup !required_minimum_index !pending in
      (* Printf.fprintf stderr "SINKING %i AS PENDING\n%!" !required_minimum_index; *)
      pending := remove !required_minimum_index !pending;
      required_minimum_index := !required_minimum_index + 1;
      sink element;
      process_pending ();
    done in
  fun ((index, element) as pair) ->
    (* Printf.fprintf stderr "@%i [WAITING FOR INDEX %i; GOT %i\n%!" (Obj.magic required_minimum_index)!required_minimum_index index; *)
    if index = !required_minimum_index then
      ((* Printf.fprintf stderr "DIRECTLY SINKING, WITHOUT TOUCHING THE CACHE; NOW WAITING FOR %i\n%!" !required_minimum_index; *)
       sink element;
       required_minimum_index := index + 1;
       process_pending ())
    else if is_in index !pending then
      (let element = lookup index !pending in
      required_minimum_index := index + 1;
      pending := remove index !pending;
      (* Printf.fprintf stderr "SINKING FROM THE CACHE; NOW WAITING FOR %i\n%!"  !required_minimum_index; *)
      sink element;
      process_pending ())
    else
      ((* Printf.fprintf stderr "CACHING %i\n%!" index; *)
       pending := insert pair !pending)

type 'b postprocessor = 'b -> unit
type ('a, 'b) skeleton = ('b postprocessor) -> ('a sink)
type ('a, 'b) instantiated_skeleton = ('a sink) * ('b source)

let instantiate_with_postprocessor skeleton postprocessor =
  skeleton postprocessor

let instantiate skeleton =
  let mailbox = make_local_mailbox () in
  (instantiate_with_postprocessor
     skeleton
     (fun result -> send mailbox result)),
  (fun () ->
    receive mailbox)

let list_map_of_instantiated_skeleton instantiated_skeleton =
  fun elements ->
    let sink, source = instantiated_skeleton in
    List.iter sink elements;
    List.map (fun _ -> source ()) elements

let feed instantiated_skeleton argument =
  let (sink, _) = instantiated_skeleton in
  sink argument

let pull instantiated_skeleton =
  let (_, source) = instantiated_skeleton in
  source ()

let trivial f =
  fun postprocessor ->
    (fun x ->
      let result = f x in
      postprocessor result)

let sequence stage1_skeleton stage2_skeleton =
  fun sequence_postprocessor ->
    instantiate_with_postprocessor
      stage1_skeleton
      (fun stage1_result ->
        (instantiate_with_postprocessor
           stage2_skeleton
           sequence_postprocessor)
          stage1_result)

let pipeline stage1_skeleton stage2_skeleton =
  fun pipeline_postprocessor ->
    let stage2_mailbox =
      split1
        (fun stage2_mailbox ->
          let stage2_sink =
            instantiate_with_postprocessor
              stage2_skeleton
              pipeline_postprocessor in
          while true do
            let stage2_parameter = receive stage2_mailbox in
            stage2_sink stage2_parameter;
          done) in
    instantiate_with_postprocessor
      stage1_skeleton
      (fun stage1_result ->
        send stage2_mailbox stage1_result)

let label unlabeled_skeleton =
  fun labeled_postprocessor ->
    let label_mailbox =
      make_local_mailbox () in
    let unlabeled_sink =
      instantiate_with_postprocessor
        unlabeled_skeleton
        (fun result ->
          (* this label is not local to the current context: *)
          let label = receive label_mailbox in
          labeled_postprocessor (label, result)) in
    (fun (label, parameter) ->
      send label_mailbox label;
      unlabeled_sink parameter)

let task_farm worker_no worker_skeleton =
  fun taskfarm_postprocessor ->
    let availability_mailbox = make_local_mailbox () in
    let collector_mailbox =
      split1
        (fun collector_mailbox ->
          let reordering_sink =
            make_reordering_sink taskfarm_postprocessor 0 in
          while true do
            let pair = receive collector_mailbox in
            reordering_sink pair;
          done) in
    let _ =
      split
        worker_no
        (fun worker_index worker_mailbox ->
          let instantiated_worker_skeleton =
            instantiate_with_postprocessor
              (label worker_skeleton)
              (fun ((_, _) as pair) -> send collector_mailbox pair) in
          while true do
            (* Tell the emitter that we're free, by sending it the mailbox he can
               use to send us tasks: *)
            send availability_mailbox worker_mailbox;
            (* Receive a parameter with its sequence number, from the emitter: *)
            let (sequence_number, parameter) = receive worker_mailbox in
            (* Printf.fprintf stderr "Worker #%i: got parameter #%i\n%!" worker_index sequence_number; *)
            (* Feed what we got to the worker, without waiting for it to finish *)
            instantiated_worker_skeleton (sequence_number, parameter);
          done) in
    let counter = ref 0 in
    (fun parameter ->
      let index = !counter in
      counter := index + 1;
      let worker_mailbox = receive availability_mailbox in
      send worker_mailbox (index, parameter))
