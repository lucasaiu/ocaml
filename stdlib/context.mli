(* Luca Saiu, REENTRANTRUNTIME *)

(* The context support unimplemented on this architecture: *)
exception Unimplemented

(* Return true iff multi-context support is implemented: *)
val implemented : unit -> bool

(* Basic context operations*)
type t

val self : unit -> t
val is_main : t -> bool
(* val is_alive : t -> bool *)

(* Splitting is not possible in the current state.  This is currently
   raised if there is more than one active thread in the splitting
   context. *)
exception CannotSplit

(* Mailboxes *)
type mailbox

val make_mailbox : unit -> mailbox

val context_of_mailbox : mailbox -> t
val is_mailbox_local : mailbox -> bool

(* These may raise CannotSplit *)
val split1 : (mailbox -> unit) -> (*new context mailbox*)mailbox
val split : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox list)
val split_into_array : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox array)

(* FIXME: do I need to expose these? *)
val split_into_context_array : int -> (int -> unit) -> (t array)
val split_into_context_list : int -> (int -> unit) -> (t list)
val split_into_context : (unit -> unit) -> t

val send : mailbox -> 'a -> unit
val receive : mailbox -> 'a


(* Wait until the context local to the given mailbox or mailboxes terminates: *)
(* FIXME: fix the multi-thread case [FIXME: is it already fixed?]*)
val join_context : t -> unit 
val join_contexts : t list -> unit
val join1 : mailbox -> unit
val join : mailbox list -> unit


(* Handlers -- an event interface *)

(* (\* Create a context which will only execute handlers. *\) *)
(* val make_handler_context : unit -> mailbox *)

(* Add a handler, to be executed *)
(* val register_handler : (mailbox -> unit) -> unit *)


(* Utility *)

(* Return the total number of CPUs in the system, counting each core or
   similar element as one unit: *)
val cpu_no : unit -> int


(* Scratch.  Horrible things which are only useful for debugging. *)

val to_string : t -> string
val sself : unit -> string

(* FIXME: these are for debugging only, and global_index in particular
   is not exactly type-safe :-) *)

(* A Caml tuple/array containing all the globals of the given context.
   The result should not be modified as it may share structure with
   the context globals.  The result may be invalidated by loading caml
   compilation units at run time (via dynlink, I suppose -- not yet
   supported). *)
val globals : Obj.t array

(* Return the index for the given global value (compared by identity)
   within the array of all globals.  Raise an exception or crash
   horribly if the given value does not correspond to any global. *)
val global_index : 'a -> int

(* FIXME: remove after debugging *)
val dump : string -> unit

(* FIXME: remove after debugging *)
val set_debugging : bool -> unit

