// waf.entity.circle — a circle from a centre and a rim point.
//
// components[0] = centre, components[1] = rim. The radius is a *distance*, not
// a stored field, so dragging either point resizes the circle — exactly how WAF
// derives it. Storing a radius instead would let the two drift apart.
//
// The shared entity fields are flat, not a nested `Entity` — see the header of
// entity.l for why.

import "apps/cad/entity.l"
import "apps/cad/entity.component.l"
import "apps/cad/entity.pos.l"

pub class Circle : IEntity {
    i32 kind
    i64 eid
    i32 visible_flag
    i64 stamp_value
    // components[0] = centre, components[1] = rim. The container is shared
    // plumbing, so it is reached by delegation, like every other entity.
    delegate CEntity
    f64 wx
    f64 wy

    pub fun init() {
        this.kind = KIND_CIRCLE
        this.eid = next_entity_id()
        this.visible_flag = 1
        this.stamp_value = 1
        this.wx = 0.0
        this.wy = 0.0
    }

    // Seed/test helper; the command layer links placed picks instead. The two
    // points go straight into the container — a local staging array would
    // auto-free them on return and leave this circle dangling.
    pub fun set_points(f64 cx, f64 cy, f64 rx, f64 ry) {
        this.detach_all()
        this.attach(heap Pos(cx, cy) as IEntity)
        this.attach(heap Pos(rx, ry) as IEntity)
        this.stamp_value = this.stamp_value + 1
    }

    // Centre / rim as values, for reading only. A `Pos` returned here is a
    // copy, so move points with `move_point`.
    pub fun centre() -> Pos {
        return (this.component_at(0) as Pos).unwrap()
    }

    pub fun rim() -> Pos {
        return (this.component_at(1) as Pos).unwrap()
    }

    pub fun update() -> bool {
        if !this.push() {
            return false
        }
        if this.component_count() != 2 {
            return false
        }
        this.wx = this.point_x(0)
        this.wy = this.point_y(0)
        this.stamp_value = this.stamp_value + 1
        return true
    }

    pub fun stamp() -> i64 {
        return this.stamp_value
    }

    pub fun kind() -> i32 {
        return this.kind
    }

    pub fun eid() -> i64 {
        return this.eid
    }

    pub fun visible() -> bool {
        return this.visible_flag != 0
    }

    pub fun set_visible(bool v) {
        if v {
            this.visible_flag = 1
        } else {
            this.visible_flag = 0
        }
    }

    // Read through the downcast rather than an interface call on the array
    // element: a shape that implements IEntity cannot reliably dispatch to a
    // component's vtables from its own methods (the receiver gets clobbered),
    // whereas `centre()` / `rim()` hand back a plain value copy.
    pub fun point_x(i32 i) -> f64 {
        if i == 0 {
            return this.centre().x
        }
        return this.rim().x
    }

    pub fun point_y(i32 i) -> f64 {
        if i == 0 {
            return this.centre().y
        }
        return this.rim().y
    }

    // Forward to the shared centre / rim point.
    pub fun move_point(i32 i, f64 x, f64 y) {
        this.component_at(i).move_point(0, x, y)
    }

    // Radius, re-derived from the two points so it cannot drift.
    pub fun metric() -> f64 {
        return dist(this.point_x(1) - this.point_x(0),
                    this.point_y(1) - this.point_y(0))
    }

    pub fun world_x() -> f64 {
        return this.wx
    }

    pub fun world_y() -> f64 {
        return this.wy
    }

    pub fun slot_count() -> i32 {
        return 2
    }
}
