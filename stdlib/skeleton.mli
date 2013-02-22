(* Luca Saiu, REENTRANTRUNTIME *)

(* Algorithmic skeletons *)

(* Core definitions *)

(* A skeleton is a graph of computing elements possibly organized to
   work in parallel, which when instantiated will consume a stream of
   inputs and produce the stream of their associated results, in the
   same order.  Computing elements not yet mapped to physical
   computing resources in a non-instantiated skeleton; in particular,
   one skeleton can be instantiated multiple times, yielding
   independent replicated graphs of computing resources. *)
type ('a, 'b) skeleton

(* We define sinks and sources as interfaces between instantiated
   skeletons and the external world, where the emphasis is on single
   objects rather than on streams.  A sink consumes an element; a
   source, when given (), produces an element.  Sinks and sources are
   named from the point of view of an observer *external* to the
   instantiated skeleton. *)
type 'a sink =   'a -> unit
type 'a source = unit -> 'a

(* An instantiated skeleton is a skeleton whose computing elements are
   associated to physical computational resouces.  An instantiated
   skeleton consumes a stream of parameters via its sink, and produces
   a stream of results (in the same order) from its source. *)
type ('a, 'b) instantiated_skeleton = ('a sink) * ('b source)

(* Simple utility functions to feed an element into, or to pull an
   element from, an instantiated skeleton.  We don't need similar
   functions for sinks and sources, which are themselves functions
   and may be trivially applied. *)
val feed : (('a, 'b) instantiated_skeleton) -> 'a -> unit
val pull : (('a, 'b) instantiated_skeleton) -> 'b

(* Allocate computing resources, turning a skeleton into an instantiated
   skeleton or a sink with side effects: *)
type 'b postprocessor = 'b -> unit (* what to do with each emitted element *)
val instantiate : (('a, 'b) skeleton) -> (('a, 'b) instantiated_skeleton)
val instantiate_with_postprocessor : (('a, 'b) skeleton) -> ('b postprocessor) -> ('a sink)

(* Given a source containing <index, element> pairs and an expected
   first index, return a source emitting the same elements, ordered by
   the indices in the output: *)
val reorder_source : (int * 'a) source -> int -> ('a source)

(* Given a sink accepting elements and an expected first index, return
   a sink accepting <index, element> pairs, which will feed elements
   to the original sink ordered by their index, as soon as each
   element with the required index is given. *)
val make_reordering_sink : 'a sink -> int -> ((int * 'a) sink)


(* Predefined skeletons *)

(* Return a skeleton computing the given function without any parallelism: *)
val trivial : ('a -> 'b) -> (('a, 'b) skeleton)

(* Given a skeleton, make another skeleton performing the same computation on
   elements with an integer label attached.  Each label is returned unchanged,
   along with the corresponding result. *)
val label : (('a, 'b) skeleton) -> ((int * 'a, int * 'b) skeleton)

(* Return the *sequential* composition of the two given skeletons.
   This is functionally equivalent to a pipeline, but the two stages
   are computed sequentially. *)
val sequence : (('a, 'b) skeleton) -> (('b, 'c) skeleton) -> (('a, 'c) skeleton)

(* Return the pipeline skeleton of the given two skeletons: the first
   one is computed first. *)
val pipeline : (('a, 'b) skeleton) -> (('b, 'c) skeleton) -> (('a, 'c) skeleton)

(* Given a number of workers and a skeleton, return a task-farm
   skeleton having a copy of the given skeleton per worker. *)
val task_farm : int -> (('a, 'b) skeleton) -> (('a, 'b) skeleton)


(* Utilities for stream programming *)

(* Given an instantiated skeleton, return a parallel version of List.map *)
val list_map_of_instantiated_skeleton : ('a, 'b) instantiated_skeleton -> (('a list) -> ('b list))
