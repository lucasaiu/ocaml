(* Luca Saiu, REENTRANTRUNTIME *)

type t

(* Experimental and not implemnted yet: BEGIN *)
type mailbox
exception ForeignMailbox of mailbox
val make_local_mailbox : unit -> mailbox
val context_of_mailbox : mailbox -> t
val is_mailbox_local : mailbox -> bool
val msplit : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox list)
val msplit1 : (mailbox -> unit) -> (*new context mailbox*)mailbox
val msend : mailbox -> 'a -> unit
val mreceive : mailbox -> 'a (* raises ForeignMailbox if the mailbox is foreign *)
(* Experimental and not implemnted yet: END *)

(* Return the total number of CPUs in the system, counting each core or
   similar element as one unit: *)
val cpu_no : unit -> int

val split : int -> (int -> unit) -> (t list)
val split_into_array : int -> (int -> unit) -> (t array)
val join : t list -> unit

(* Make a new context in which the given function will be exectuted,
   within a new thread.  Return the new context. *)
val split1 : (unit -> unit) -> t
val join1 : t -> unit

(* (\* Start as many contexts as the given integer, running the given *)
(*    function in each one.  Each function takes a 0-based index as its *)
(*    parameter.  Return the new contexts. *\) *)
(* val fork_many : int -> (int -> unit) -> (t list) *)

(* (\* Exit the process, killing the current context. *\) *)
(* val exit : unit -> unit *)

val self : unit -> t
val is_main : t -> bool
val is_remote : t -> bool

val send : t -> 'a -> unit
val receive : unit -> (t * 'a)

(*
val send_to_any : 'a -> (t list) -> unit
val receive_from_any : (t list) -> 'a
*)
val to_string : t -> string
val sself : unit -> string

(* FIXME: these are for debugging only, and global_index in particular
   is not exactly type-safe :-) *)
val globals : unit -> 'a
(* val globals_and_datum : 'a -> ('b * 'a) *)

(* Return the index for the given global value (compared by identity)
   within the array of all globals.  Raise an exception or crash
   horribly if the given value does not correspond to any global. *)
val global_index : 'a -> int

(* val dump : unit -> int *)

(* Given a number of workers and a sequential function f, return a
   parallel version of (List.map f).  The processed list can have any
   length *)
(* val taskfarm : int -> ('a -> 'b) -> ('a list -> 'b list) *)
val taskfarm : int -> ('a -> 'b) -> ('a list -> (int * 'b) list)
