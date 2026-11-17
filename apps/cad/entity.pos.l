// waf.entity.pos — the free point.
//
// A point is the only entity with real coordinates; every shape is a relation
// between points. The marker arm is measured in *pixels* on screen, so the
// cross stays the same size at every zoom (WAF: CPos::arm_mm = arm_px /
// canvas_scale).
//
// The shared entity fields are flat, not a nested `Entity` — see the header of
// entity.l for why.

import "apps/cad/entity.l"
import "apps/cad/entity.component.l"

pub class Pos : IEntity {
    i32 kind
    i64 eid
    i32 visible_flag
    i64 stamp_value
    // A point has no inputs. The container is here only because every entity
    // carries one (CEntity's `components`, reached by delegation) and the scene
    // treats them uniformly.
    delegate CEntity
    f64 x
    f64 y
    f64 wx
    f64 wy
    f64 arm_px

    pub fun init(f64 px, f64 py) {
        this.kind = KIND_POS
        this.eid = next_entity_id()
        this.visible_flag = 1
        this.stamp_value = 1
        this.x = px
        this.y = py
        this.wx = px
        this.wy = py
        this.arm_px = 7.0
    }

    // The only mutation a point accepts. Moving *is* the rebuild: its geometry
    // is its position, so nothing has to be re-derived.
    pub fun move_to(f64 px, f64 py) {
        if px == this.x && py == this.y {
            return
        }
        this.x = px
        this.y = py
        this.wx = px
        this.wy = py
        this.stamp_value = this.stamp_value + 1
    }

    // A point has no inputs; updating is a no-op that always succeeds.
    pub fun update() -> bool {
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

    // Component slots come from the delegated CEntity. A point has no inputs,
    // so the list is always empty; the slot exists so the scene can treat every
    // entity uniformly.
    pub fun component_count() -> i32 {
        return 0
    }

    pub fun component_at(i32 i) -> IEntity {
        IEntity self = this as IEntity
        return self
    }

    pub fun point_x(i32 i) -> f64 {
        return this.x
    }

    pub fun point_y(i32 i) -> f64 {
        return this.y
    }

    // Slot 0 is this point. The command layer moves a point by holding it as an
    // `IEntity` and calling this, which reaches the one shared object rather
    // than a copy.
    pub fun move_point(i32 i, f64 px, f64 py) {
        this.move_to(px, py)
    }

    pub fun metric() -> f64 {
        return 0.0
    }

    pub fun world_x() -> f64 {
        return this.x
    }

    pub fun world_y() -> f64 {
        return this.y
    }

    // A point already is what a pick places; there is nothing to link.
    pub fun set_points(f64 ax, f64 ay, f64 bx, f64 by) {
        this.move_to(ax, ay)
    }

    pub fun slot_count() -> i32 {
        return 1
    }

    // World half-length of the cross at the given canvas scale (px per mm).
    pub fun arm_mm(f64 canvas_scale) -> f64 {
        if canvas_scale <= 0.0 {
            return 0.0
        }
        return this.arm_px / canvas_scale
    }
}
