// waf.scene — the non-owning draw index.
//
// Corresponds to CCanvas::scene_: a list of entities to draw, nothing more.
// Ownership lives in the command tree / app; a shape can therefore sit in the
// scene while its endpoints are still being picked, and leaving the scene does
// not destroy it.
//
// The list itself is `CEntitys`, embedded by `delegate` (entity.component.l),
// so "a set of entities" has exactly one implementation. What stays here is the
// part only a scene can answer: picking, and the top-down update walk.
// Insertion order is draw order: the most recently added shape is on top.

import "apps/cad/entity.l"
import "apps/cad/entity.component.l"
import "apps/cad/entity.pos.l"
import "apps/cad/entity.line.l"
import "apps/cad/entity.circle.l"

pub class Scene {
    delegate CEntitys

    pub fun init() {
    }

    // Top-down refresh: each shape pushes its own components. Cheap enough for
    // a drawing of this size, and it keeps the shapes free of back-references.
    pub fun update() {
        i32 i = 0
        while i < this.len() {
            IEntity it = this.get(i)
            it.update()
            i = i + 1
        }
    }

    // Nearest point within `tol` world units of (x, y). Used for snapping and
    // for picking a grip. Points are entities, so a shared point is one object
    // however many shapes read it.
    pub fun pick_point(f64 x, f64 y, f64 tol) -> Optional<IEntity> {
        Optional<IEntity> best = Optional<IEntity>()
        f64 best_d = tol * tol
        i32 i = 0
        while i < this.len() {
            IEntity it = this.get(i)
            if KIND_POS == it.kind() && it.visible() {
                f64 dx = it.world_x() - x
                f64 dy = it.world_y() - y
                f64 d = dx * dx + dy * dy
                if d < best_d {
                    best_d = d
                    best = Optional<IEntity>(true, it)
                }
            }
            i = i + 1
        }
        return best
    }

    // Nearest line within `tol` of (x, y), by perpendicular distance. Needed to
    // snap onto an existing edge, not just onto its endpoints.
    pub fun pick_line(f64 x, f64 y, f64 tol) -> Optional<IEntity> {
        Optional<IEntity> best = Optional<IEntity>()
        f64 best_d = tol
        i32 i = 0
        while i < this.len() {
            IEntity it = this.get(i)
            if KIND_LINE == it.kind() && it.visible() {
                f64 d = point_segment_dist(x, y, it.point_x(0), it.point_y(0),
                                           it.point_x(1), it.point_y(1))
                if d < best_d {
                    best_d = d
                    best = Optional<IEntity>(true, it)
                }
            }
            i = i + 1
        }
        return best
    }

    // Nearest circle outline within `tol`: |distance to centre - radius|.
    pub fun pick_circle(f64 x, f64 y, f64 tol) -> Optional<IEntity> {
        Optional<IEntity> best = Optional<IEntity>()
        f64 best_d = tol
        i32 i = 0
        while i < this.len() {
            IEntity it = this.get(i)
            if KIND_CIRCLE == it.kind() && it.visible() {
                f64 d = dist(it.world_x() - x, it.world_y() - y) - it.metric()
                if d < 0.0 {
                    d = 0.0 - d
                }
                if d < best_d {
                    best_d = d
                    best = Optional<IEntity>(true, it)
                }
            }
            i = i + 1
        }
        return best
    }
}

// Perpendicular distance from (px, py) to segment a→b, clamped to the endpoints
// so a point beyond the segment measures to the nearer end.
pub fun point_segment_dist(f64 px, f64 py, f64 ax, f64 ay, f64 bx, f64 by) -> f64 {
    f64 vx = bx - ax
    f64 vy = by - ay
    f64 len2 = vx * vx + vy * vy
    if len2 <= 0.0 {
        return dist(px - ax, py - ay)
    }
    f64 t = ((px - ax) * vx + (py - ay) * vy) / len2
    if t < 0.0 {
        t = 0.0
    }
    if t > 1.0 {
        t = 1.0
    }
    return dist(px - (ax + t * vx), py - (ay + t * vy))
}
