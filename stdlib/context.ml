(* Luca Saiu, REENTRANTRUNTIME *)

(* FIXME: use a custom type instead *)
type t =
  int

(* type fork_result = *)
(*   | NewContext of t *)
(*   | OldContext *)

(* let fork () = failwith "unimplemented" *)
(* let exit () = failwith "unimplemented" *)

external self : unit -> t = "caml_context_self_r" "reentrant"
external is_main : t -> bool = "caml_context_is_main_r" "reentrant"
external is_remote : t -> bool = "caml_context_is_remote_r" "reentrant"

(* let fork () = *)
(*   let returned_context = fork_low_level () in *)
(*   if returned_context = (self ()) then *)
(*     OldContext *)
(*   else *)
(*     NewContext returned_context *)

(* let rec fork_many n f = *)
(*   if n < 0 then *)
(*     (failwith "fork_many: negative argument"); *)
(*   if n > 0 then *)
(*   match fork () with *)
(*   | NewContext _ -> *)
(*       f (n - 1) *)
(*   | OldContext -> *)
(*       fork_many (n - 1) f *)

(* let fork thunk = *)
(*   failwith "unimplemented" *)
external fork : (unit -> unit) -> t = "caml_context_fork_and_run_thunk_r" "reentrant"
external pthread_create : (unit -> unit) -> t = "caml_context_pthread_create_and_run_thunk_r" "reentrant"


(* (iota n) returns the int list [0, n).  The name comes from APL, and
   has also been adopted by Guile Scheme. *)
let rec iota_acc n a =
  if n < 0 then
    a
  else
    iota_acc (n - 1) (n :: a)
let iota n =
  iota_acc (n - 1) []

let fork_many n f =
  List.map
    (fun i ->
      fork (fun () -> f i))
    (iota n);;

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

external exit : unit -> unit = "caml_context_exit_r" "reentrant"
let send message receiver_context = failwith "unimplemented"
let receive receiver_context = failwith "unimplemented"

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

(* external dump : unit -> int = "caml_context_dump_r" "reentrant" *)
