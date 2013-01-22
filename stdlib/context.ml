(* Luca Saiu, REENTRANTRUNTIME *)

(* FIXME: use a custom type instead *)
type t =
  int

external cpu_no : unit -> int = "caml_cpu_no_r" "reentrant"

external self : unit -> t = "caml_context_self_r" "reentrant"
external is_main : t -> bool = "caml_context_is_main_r" "reentrant"
external is_remote : t -> bool = "caml_context_is_remote_r" "reentrant"

external split_into_array : (int -> unit) -> int -> (t array) = "caml_context_split_r" "reentrant"

let split f how_many =
  Array.to_list (split_into_array f how_many)

(* FIXME: remove *)
(* external pthread_create : (int -> unit) -> t = "caml_context_pthread_create_and_run_thunk_r" "reentrant" *)
let split1 thunk =
  List.hd (split (fun i -> thunk ()) 1)

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

(* Mailboxes: not implemented yet *)
type mailbox = int
let msplit context_no f =
  failwith "msplit: unimplemented"
let msend mailbox message =
  failwith "msend: unimplemented"
let mreceive mailbox =
  failwith "mreceive: unimplemented"
let make_local_mailbox () =
  failwith "make_local_mailbox: unimplemented"
