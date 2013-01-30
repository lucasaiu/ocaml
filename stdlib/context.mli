(* Luca Saiu, REENTRANTRUNTIME *)

(* Basic context operations*)
type t

val self : unit -> t
val is_main : t -> bool
(* val is_alive : t -> bool *)


(* Mailboxes *)
type mailbox

(* exception ForeignMailbox of mailbox (\* FIXME: this should be removed *\) *)

val make_local_mailbox : unit -> mailbox

val context_of_mailbox : mailbox -> t
val is_mailbox_local : mailbox -> bool

val split1 : (mailbox -> unit) -> (*new context mailbox*)mailbox
val split : int -> (int -> mailbox -> unit) -> (*mailboxes to new contexts*)(mailbox list)

val send : mailbox -> 'a -> unit
val receive : mailbox -> 'a (* raises ForeignMailbox if the mailbox is foreign *)

(* val mjoin1 : mailbox -> unit *)
(* val mjoin : (mailbox list) -> unit *)

(* Wait until the context local to the given mailbox or mailboxes terminates: *)
val join1 : mailbox -> unit
val join : mailbox list -> unit


(* Algorithmic skeletons *)

(* A skeleton is a graph of computing elements possibly organized to
   work in parallel, which when instantiated will consume a stream of
   inputs and produce the stream of their associated results, in the
   same order.  Computing elements are abstract in a non-instantiated
   skeleton, and they are not yet mapped to physical computing
   resources.  In particular, the same skeleton can be instantiated
   multiple times, yielding independent replicated graphs of computing
   resources. *)
type ('a, 'b) skeleton

(* We define sinks and sources as interfaces between streams and
   instantiated skeletons.  A sink consumes an element, and a source,
   when given (), produces an element.  Sinks and sources are named
   from the point of view of an observer *external* to the skeleton. *)
type 'a sink =   'a -> unit
type 'a source = unit -> 'a

(* An instantiated skeleton is a skeleton whose computing elements are
   associated to physical computational resouces.  An instantiated
   skeleton consumes a stream of parameters via its sink, and produces
   a stream of results (in the same order) from its source.
   Instantiation allocates computational resources for a skeleton. *)
type ('a, 'b) instantiated_skeleton = ('a sink) * ('b source)

(* Trivial utility functions to feed an element into, or to pull an
   element from, an instantiated skeleton.  We don't need similar
   functions for sinks and sources, which can be simply applied. *)
val feed : (('a, 'b) instantiated_skeleton) -> 'a -> unit
val pull : (('a, 'b) instantiated_skeleton) -> 'b

(* Turn a skeleton into an instantiated skeleton or a sink with side
   effects, allocating computing resources: *)
type 'b postprocessor = 'b -> unit
val instantiate_with_postprocessor : (('a, 'b) skeleton) -> ('b postprocessor) -> ('a sink)
val instantiate : (('a, 'b) skeleton) -> (('a, 'b) instantiated_skeleton)

(* Given a source containing <index, element> pairs and an expected
   first index, return a source emitting the same elements, ordered by
   the indices in the output: *)
val reorder_source : (int * 'a) source -> int -> ('a source)

(* Given a sink accepting elements and an expected first index, return
   a sink accepting <index, element> pairs, which will feed elements
   to the original sink ordered by their index, as soon as each
   element with the required index is given. *)
val make_reordering_sink : 'a sink -> int -> ((int * 'a) sink)

(* Given a skeleton, make another skeleton performing the same computation on data
   with an integer label attached. *)
val label : (('a, 'b) skeleton) -> ((int * 'a, int * 'b) skeleton)

(* Given an instantiated skeleton, return a parallel version of List.map *)
val list_map_of_instantiated_skeleton : ('a, 'b) instantiated_skeleton -> ('a list) -> ('b list)

(* Return a skeleton computing the given function without any parallelism: *)
val trivial : ('a -> 'b) -> (('a, 'b) skeleton)

(* Return the *sequential* composition of the two given skeletons.
   This is functionally equivalent to a pipeline, but the two stages
   are computed sequentially. *)
val sequence : (('a, 'b) skeleton) -> (('b, 'c) skeleton) -> (('a, 'c) skeleton)

(* Given a number of workers and a skeleton, return a task-farm
   skeleton computing the given skeleton in the workers *)
val task_farm : int -> (('a, 'b) skeleton) -> (('a, 'b) skeleton)

(* Return the pipeline skeleton of the given two skeletons: the first
   one is computed first. *)
val pipeline : (('a, 'b) skeleton) -> (('b, 'c) skeleton) -> (('a, 'c) skeleton)


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
