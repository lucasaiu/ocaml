(* Luca Saiu, REENTRANTRUNTIME *)

type 'a sink =   'a -> unit
type 'a source = unit -> 'a

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
  let mailbox = Context.make_mailbox () in
  (instantiate_with_postprocessor
     skeleton
     (fun result -> Context.send mailbox result)),
  (fun () ->
    Context.receive mailbox)

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
    let stage2_sink =
      instantiate_with_postprocessor
        stage2_skeleton
        sequence_postprocessor in
    let stage1_sink =
      instantiate_with_postprocessor
        stage1_skeleton
        (fun stage1_result ->
          stage2_sink stage1_result) in
    stage1_sink

let pipeline stage1_skeleton stage2_skeleton =
  fun pipeline_postprocessor ->
    let stage2_mailbox =
      Context.split1
        (fun stage2_mailbox ->
          let stage2_sink =
            instantiate_with_postprocessor
              stage2_skeleton
              pipeline_postprocessor in
          while true do
            let stage2_parameter = Context.receive stage2_mailbox in
            stage2_sink stage2_parameter;
          done) in
    instantiate_with_postprocessor
      stage1_skeleton
      (fun stage1_result ->
        Context.send stage2_mailbox stage1_result)

let label unlabeled_skeleton =
  fun labeled_postprocessor ->
    let label_mailbox =
      Context.make_mailbox () in
    let unlabeled_sink =
      instantiate_with_postprocessor
        unlabeled_skeleton
        (fun result ->
          (* this label is not local to the current context: *)
          let label = Context.receive label_mailbox in
          labeled_postprocessor (label, result)) in
    (fun (label, parameter) ->
      Context.send label_mailbox label;
      unlabeled_sink parameter)

let task_farm worker_no worker_skeleton =
  fun taskfarm_postprocessor ->
    let availability_mailbox = Context.make_mailbox () in
    let collector_mailbox =
      Context.split1
        (fun collector_mailbox ->
          let reordering_sink =
            make_reordering_sink taskfarm_postprocessor 0 in
          while true do
            let pair = Context.receive collector_mailbox in
            reordering_sink pair;
          done) in
    let _ =
      Context.split
        worker_no
        (fun worker_index worker_mailbox ->
          let instantiated_worker_skeleton =
            instantiate_with_postprocessor
              (label worker_skeleton)
              (fun ((_, _) as pair) -> Context.send collector_mailbox pair) in
          while true do
            (* Tell the emitter that we're free, by Context.sending it the mailbox he can
               use to Context.send us tasks: *)
            Context.send availability_mailbox worker_mailbox;
            (* Receive a parameter with its sequence number, from the emitter: *)
            let (sequence_number, parameter) = Context.receive worker_mailbox in
            (* Printf.fprintf stderr "Worker #%i: got parameter #%i\n%!" worker_index sequence_number; *)
            (* Feed what we got to the worker, without waiting for it to finish *)
            instantiated_worker_skeleton (sequence_number, parameter);
          done) in
    let counter = ref 0 in
    (fun parameter ->
      let index = !counter in
      counter := index + 1;
      let worker_mailbox = Context.receive availability_mailbox in
      Context.send worker_mailbox (index, parameter))
