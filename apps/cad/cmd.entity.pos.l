// waf.cmd.entity.pos — the point placement command (CCmdEntity + CPos).
//
// One pick: the position. The picks live in the command, not in the scene, so
// cancelling a half-finished command leaves the drawing untouched.
//
// Picks are entities, not loose numbers: the command holds a `CEntitys` set
// (the same container a `Scene` is made of) through `delegate`, so a pick is a
// slot that `commit` hands straight to `set_points`. Each command only decides
// how many slots it needs and which entity it mints.

import "stdlib/memory.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "apps/cad/entity.l"
import "apps/cad/entity.pos.l"
import "apps/cad/entity.component.l"

pub class CmdEntityPos : ICmd {
    delegate CEntitys

    pub fun needed() -> i32 {
        return 1
    }

    // ICmd spells the slot count `count`, the container spells it `len`; one
    // forwarder keeps the interface satisfied instead of a vtable thunk.
    pub fun count() -> i32 {
        return this.len()
    }

    // Fills the next free slot. A command never holds more picks than it needs;
    // `count` therefore doubles as "how far along the gesture is".
    pub fun push(f64 x, f64 y) {
        i32 n = this.len()
        if n < this.needed() {
            this.add(heap Pos(x, y) as IEntity)
            return
        }
        this.set(n - 1, heap Pos(x, y) as IEntity)
    }

    pub fun clear() {
        this.clear_all()
    }

    pub fun px(i32 i) -> f64 {
        if i >= this.len() {
            return 0.0
        }
        return this.get(i).world_x()
    }

    pub fun py(i32 i) -> f64 {
        if i >= this.len() {
            return 0.0
        }
        return this.get(i).world_y()
    }

    pub fun ready() -> bool {
        return this.len() >= this.needed()
    }

    pub fun kind() -> i32 {
        return KIND_POS
    }

    pub fun kind_name() -> String {
        return String("POINT")
    }

    // A point is heap cells and therefore reachable from the scene; see
    // entity.line.l for why a fresh shape must not stay on the stack.
    pub fun make() -> IEntity {
        return heap Pos(0.0, 0.0) as IEntity
    }
}
