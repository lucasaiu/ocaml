(* Luca Saiu, REENTRANTRUNTIME *)

(* Basic context operations*)
type t

val self : unit -> t
val is_main : t -> bool

(* Make a new context in which the given function will be exectuted,
   within a new thread.  Return the new context. *)
val split1 : (unit -> unit) -> t

val split : int -> (int -> unit) -> (t list)
val split_into_array : int -> (int -> unit) -> (t array)

val join1 : t -> unit
val join : t list -> unit


(* Mailboxes *)
type mailbox

exception ForeignMailbox of mailbox

val make_local_mailbox : unit -> mailbox

val context_of_mailbox : mailbox -> t
val is_mailbox_local : mailbox -> bool

val msplit1 : (mailbox -> unit) -> (*new context mailbox*)mailbox
val msplit : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox list)

val msend : mailbox -> 'a -> unit
val mreceive : mailbox -> 'a (* raises ForeignMailbox if the mailbox is foreign *)


(* Algorithmic skeletons *)
type 'a sink =   'a -> unit
type 'a source = unit -> 'a
type ('a, 'b) skeleton = ('a sink) * ('b source)

(* Given a source containing <index, element> pairs and an expected
   first index, return a source emitting the same elements, ordered by
   the indices in the output: *)
val reorder_source : (int * 'a) source -> int -> ('a source)

val list_map_of_skeleton : ('a, 'b) skeleton -> ('a list) -> ('b list)

(* Given a number of workers and a sequential function f, return a
   parallel version of (List.map f).  The processed list can have any
   length: *)
val task_farm : int -> ('a -> 'b) -> (('a, 'b) skeleton)


(* Utility *)

(* Return the total number of CPUs in the system, counting each core or
   similar element as one unit: *)
val cpu_no : unit -> int


(* Scratch.  Horrible things which are only useful for debugging. *)

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
