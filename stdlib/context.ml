(* Luca Saiu, REENTRANTRUNTIME *)

(* FIXME: use a custom type instead *)
type t =
  int

external cpu_no : unit -> int = "caml_cpu_no_r" "reentrant"

external self : unit -> t = "caml_context_self_r" "reentrant"
external is_main : t -> bool = "caml_context_is_main_r" "reentrant"
external is_remote : t -> bool = "caml_context_is_remote_r" "reentrant"

external split_into_array : int -> (int -> unit) -> (t array) = "caml_context_split_r" "reentrant"

let split how_many f =
  Array.to_list (split_into_array how_many f)

(* FIXME: remove *)
(* external pthread_create : (int -> unit) -> t = "caml_context_pthread_create_and_run_thunk_r" "reentrant" *)
let split1 thunk =
  List.hd (split 1 (fun i -> thunk ()))

(* (iota n) returns the int list [0, n).  The name comes from APL, and
   has also been adopted by Guile Scheme. *)
let rec iota_acc n a =
  if n < 0 then
    a
  else
    iota_acc (n - 1) (n :: a)
let iota n =
  iota_acc (n - 1) []

(* let fork_many n f = *)
(*   List.map *)
(*     (fun i -> *)
(*       fork (fun () -> f i)) *)
(*     (iota n);; *)

(* let rec apply_functions functions list = *)
(*   match list, functions with *)
(*   | [], [] -> *)
(*       [] *)
(*   | (first_f :: more_fs), (first_x :: more_xs) -> *)
(*       (first_f first_x) :: (apply_functions more_fs more_xs);; *)
(*   | _, [] -> *)
(*       failwith "apply_functions: too many functions" *)
(*   | [], _ -> *)
(*       failwith "apply_functions: too many arguments" *)

(* let fork_many n f = *)
(*   let results = ref [] in *)
(*   for i = n - 1 downto 0 do *)
(*     let context = fork (fun () -> f i) in *)
(*     results := context :: !results; *)
(*   done; *)
(*   !results *)

(* external exit : unit -> unit = "caml_context_exit_r" "reentrant" *)
external send : t -> 'a -> unit = "caml_context_send_r" "reentrant"
external receive : unit -> (t * 'a) = "caml_context_receive_r" "reentrant"

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

external join1 : t -> unit = "caml_context_join_r" "reentrant"

let join contexts =
  List.iter join1 contexts

(* Temporary, horrible, inefficient and generally revolting implemantation of mailboxes: *)
type mailbox = t * int
exception ForeignMailbox of mailbox

let local_mailbox_counter = ref 0

let make_local_mailbox () =
  let index = !local_mailbox_counter in
  local_mailbox_counter := index + 1;
  (self ()), index

let context_of_mailbox (context, _) =
  context

let is_mailbox_local (context, _) =
  (self ()) = context

let msplit context_no f =
  let contexts =
    split
      context_no
      (fun index ->
        local_mailbox_counter := 0; (* reset the local index *)
        f index (make_local_mailbox ())) in
  List.map (fun context -> (context, 0)) contexts;;

let msplit1 f =
  List.hd (msplit 1 (fun _ mailbox -> f mailbox))

let msend mailbox message =
  let context, index = mailbox in
  send context (index, message)

let rec mreceive mailbox =
  if is_mailbox_local mailbox then
    let context, mailbox_index = mailbox in
    let (*sender*)_, (message_index, message) = receive () in
    Printf.fprintf stderr "%i,%i: received a message for ourself.  Good\n" context mailbox_index; flush stderr;
    if mailbox_index = message_index then
      message
    else
      (* This solution is only correct on an UNBOUNDED-LENGTH queue. *)
      (* Re-enqueue the message which was for the other mailbox, and try again: *)
      (Printf.fprintf stderr "%i,%i: received a message for %i,%i; trying again\n" context mailbox_index context message_index; flush stderr;
       msend (context, message_index) message;
       mreceive mailbox)
  else
    raise (ForeignMailbox mailbox)


let taskfarm worker_no work_function =
  let collector_mailbox = make_local_mailbox () in
  let worker_mailboxes =
    msplit
      worker_no
      (fun worker_index worker_mailbox ->
        let availability_mailbox = mreceive worker_mailbox in
        while true do
          (* Tell the emitter that we're free, by sending it the mailbox he can
             use to send us tasks: *)
          msend availability_mailbox worker_mailbox;
          (* Receive a parameter, compute on it, and send the result: *)
          let (sequence_number, parameter) = mreceive worker_mailbox in
          let result = work_function parameter in
          msend collector_mailbox (sequence_number, result);
        done) in
  let emitter_mailbox =
    msplit1
      (fun emitter_mailbox ->
        let availability_mailbox = make_local_mailbox () in
        (* Send the mailbox used to signal that a worker is available to workers: *)
        List.iter
          (fun worker_mailbox -> msend worker_mailbox availability_mailbox)
          worker_mailboxes;
        let counter = ref 0 in
        while true do
          (* Get a task: *)
          let parameter_index = !counter in
          counter := parameter_index + 1;
          let parameter = mreceive emitter_mailbox in
          (* Get a free worker mailbox (waiting if needed): *)
          let worker_mailbox = mreceive availability_mailbox in
          (* Send the task to the worker: *)
          msend worker_mailbox (parameter_index, parameter);
        done) in
  (fun parameters ->
    List.iter
      (fun parameter -> msend emitter_mailbox parameter)
      parameters;
    let results = ref [] in
    for i = 1 to List.length parameters do
      ignore i; (* this is silly, but the compiler complains when I don't use i, and I can't have a pattern instead of the variable as the for loop index *)
      results := (mreceive collector_mailbox) :: !results;
    done;
    (* List.rev_map *)
    (*   snd *)
      (List.sort
         (fun (index1, _) (index2, _) -> (*reversed compare*)compare index2 index1)
         !results))
