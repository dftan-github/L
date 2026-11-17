// waf.entity — the entity base of the drawing model.
//
// The idea that matters: a line does NOT own its endpoints as private fields.
// It holds *components* — standalone `Pos` entities that other shapes can
// share. There is exactly one point object, so moving it moves every line and
// circle that reads it.
//
// WAF's CEntity keeps `refs_` (who reads me) to push dirtiness upward in
// O(fan-in). This version updates top-down: `Scene::update()` walks the shapes
// and each shape pushes its own components first. Same composition, without the
// reverse index.
//
// Two L limitations decide the shape of these files:
//
//   1. No inheritance. WAF's `virtual update()` becomes `IEntity` plus a `kind`
//      tag. Every shape implements `IEntity` and embeds the shared input
//      container `CEntity` through `delegate`, so `components` is declared once
//      (entity.component.l) instead of in every class. The remaining shared
//      fields (kind / eid / visible / stamp) stay flat per shape.
//
//   2. Class-typed getters return copies. `Array<Class>.get(i)` hands back a
//      copy, so mutating through it is a silent no-op; `Array<IEntity>.get(i)`
//      keeps the original address (the fat pointer still points at the one
//      object). All point mutation therefore goes through the interface, via
//      `move_point`.
//
// Layout mirrors WAF:
//   entity.l           — this file: IEntity + shared helpers
//   entity.component.l — CEntity   (the component list, embedded by delegate)
//   entity.pos.l       — CPos      (a free point)
//   entity.line.l      — CLine     (two point components)
//   entity.circle.l    — CCircle   (centre + rim components)
//   scene.l            — the non-owning draw index (CCanvas::scene_)

import "stdlib/memory.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "stdlib/io.l"

const KIND_POS = 1
const KIND_LINE = 2
const KIND_CIRCLE = 3

// Stable identity for entities, handed out at construction. Used for equality
// and removal; L offers no pointer to compare interface values with.
i64 g_next_eid = 0

pub fun next_entity_id() -> i64 {
    g_next_eid = g_next_eid + 1
    return g_next_eid
}

// What the scene and the command layer need from anything placeable.
// Mirrors CEntity::update(): refresh from your inputs, return false when they
// are not ready.
//
// Deliberately free of `Pos`: Pos implements this interface, so naming it here
// would be circular. Component coordinates are read through the flat
// `point_x` / `point_y` accessors instead.
pub interface IEntity {
    fun update() -> bool
    // Bumped whenever the shape's geometry changed; the canvas caches meshes
    // by this and nothing else.
    fun stamp() -> i64
    fun kind() -> i32
    // Stable identity. L exposes no pointer for interface values, so equality
    // and removal go through this.
    fun eid() -> i64
    fun visible() -> bool
    fun set_visible(bool v)
    fun component_count() -> i32
    fun component_at(i32 i) -> IEntity
    // World coordinates of component slot i (a point).
    fun point_x(i32 i) -> f64
    fun point_y(i32 i) -> f64
    // Moves component slot i to (x, y). The only way to move a shared point:
    // a class-typed getter would return a copy and silently do nothing.
    fun move_point(i32 i, f64 x, f64 y)
    // The shape's own derived measurement: radius for a circle, length for a
    // line, 0 for a point. Read through the interface for the same reason.
    fun metric() -> f64
    // Derived world position: point, midpoint, or centre.
    fun world_x() -> f64
    fun world_y() -> f64
    // Link the picks a command has collected, the way WAF's CPos/CLine/CCircle
    // accept their reference points. A point ignores its arguments.
    fun set_points(f64 ax, f64 ay, f64 bx, f64 by)
    // Geometry the canvas strokes. A point draws a cross at world_x/world_y, a
    // line a segment between two slots, a circle an ellipse about its centre.
    // The renderer asks `kind()` which of those three it is, then reads the
    // numbers through these accessors — never a concrete class.
    //   slot_count — how many input slots this entity exposes
    fun slot_count() -> i32
}

// ---- shared helpers ----------------------------------------------------
//
// What is left once the input container moved out: predicates over entities.
// The component list itself is `CEntity` in entity.component.l, embedded by
// `delegate` rather than repeated as an `Array<IEntity>` field per shape.

pub fun same_entity(IEntity a, IEntity b) -> bool {
    return a.eid() == b.eid()
}

pub fun dist(f64 dx, f64 dy) -> f64 {
    f64 d = dx * dx + dy * dy
    if d <= 0.0 {
        return 0.0
    }
    f64 x = d
    i32 i = 0
    while i < 24 {
        x = 0.5 * (x + d / x)
        i = i + 1
    }
    return x
}
