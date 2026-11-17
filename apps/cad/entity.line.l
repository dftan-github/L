// waf.entity.line — a line between two points.
//
// Two component slots, nothing else. The endpoints are *shared* points, not
// copies: a line drawn from an existing point and a new one reads the very
// same object, so dragging that point drags the line.
//
// The midpoint is derived in update() and written to wx/wy; it is never stored
// by a caller.
//
// The shared entity fields are flat, not a nested `Entity` — see the header of
// entity.l for why.

import "apps/cad/entity.l"
import "apps/cad/entity.component.l"
import "apps/cad/entity.pos.l"

pub class Line : IEntity {
    i32 kind
    i64 eid
    i32 visible_flag
    i64 stamp_value
    // The endpoints, components[0] = start, components[1] = end. Non-owning;
    // the container is shared plumbing, so it is reached by delegation.
    delegate CEntity
    f64 wx
    f64 wy

    pub fun init() {
        this.kind = KIND_LINE
        this.eid = next_entity_id()
        this.visible_flag = 1
        this.stamp_value = 1
        this.wx = 0.0
        this.wy = 0.0
    }

    // Makes two fresh endpoints and wires them in. The command layer does not
    // use this — it links already-placed picks — but seeds and tests do.
    //
    // The points are heap cells handed straight to the container: staging them
    // in a local array would hand ownership to that array, whose auto-free
    // would l_free them on return and leave this line dangling.
    pub fun set_endpoints(f64 ax, f64 ay, f64 bx, f64 by) {
        this.detach_all()
        this.attach(heap Pos(ax, ay) as IEntity)
        this.attach(heap Pos(bx, by) as IEntity)
        this.stamp_value = this.stamp_value + 1
    }

    // The endpoints as values. Only for reading: `Pos` is a copy here, so a
    // caller must not try to move through it (use move_point).
    pub fun a() -> Pos {
        return (this.component_at(0) as Pos).unwrap()
    }

    pub fun b() -> Pos {
        return (this.component_at(1) as Pos).unwrap()
    }

    pub fun update() -> bool {
        if !this.push() {
            return false
        }
        if this.component_count() != 2 {
            return false
        }
        this.wx = (this.point_x(0) + this.point_x(1)) * 0.5
        this.wy = (this.point_y(0) + this.point_y(1)) * 0.5
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
    // whereas a() / b() hand back a plain value copy.
    pub fun point_x(i32 i) -> f64 {
        if i == 0 {
            return this.a().x
        }
        return this.b().x
    }

    pub fun point_y(i32 i) -> f64 {
        if i == 0 {
            return this.a().y
        }
        return this.b().y
    }

    // Forward to the shared endpoint. Must go through the interface element,
    // not a()/b(): those return a copy, so moving through them is a no-op.
    pub fun move_point(i32 i, f64 x, f64 y) {
        this.component_at(i).move_point(0, x, y)
    }

    // Length, re-derived rather than stored so it cannot drift.
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

    // Links two placed picks as the endpoints. The command layer allocates the
    // points and hands them over, so the line shares them instead of copying.
    pub fun set_points(f64 ax, f64 ay, f64 bx, f64 by) {
        this.detach_all()
        this.attach(heap Pos(ax, ay) as IEntity)
        this.attach(heap Pos(bx, by) as IEntity)
        this.stamp_value = this.stamp_value + 1
    }

    pub fun slot_count() -> i32 {
        return 2
    }
}
