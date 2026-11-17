// waf.cmd.entity.line — the line placement command (CCmdEntity + CLine).
//
// Two picks: start, end. The picks live in the command, not in the scene, so
// cancelling a half-finished line leaves the drawing untouched.
//
// Picks are entities: the command embeds the same `CEntitys` set a `Scene` is
// made of (`delegate`), so slot 0 and slot 1 are the very points `commit`
// links into the new line via `set_points`.

import "stdlib/memory.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "apps/cad/entity.l"
import "apps/cad/entity.line.l"
import "apps/cad/entity.component.l"

pub class CmdEntityLine : ICmd {
    delegate CEntitys

    pub fun needed() -> i32 {
        return 2
    }

    // ICmd spells the slot count `count`, the container spells it `len`.
    pub fun count() -> i32 {
        return this.len()
    }

    // Fills the next free slot; once both are there, the last one moves
    // (that is what a live preview poking the second endpoint expects).
    pub fun push(f64 x, f64 y) {
        i32 n = this.len()
        if n < this.needed() {
            this.add(heap Pos(x, y) as IEntity)
            return
        }
        this.set(n - 1, heap Pos(x, y) as IEntity)
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

    pub fun clear() {
        this.clear_all()
    }

    pub fun kind() -> i32 {
        return KIND_LINE
    }

    pub fun kind_name() -> String {
        return String("LINE")
    }

    // Heap cell, not a stack local: the scene borrows this entity for as long
    // as the drawing lives, so its address must outlive the command call.
    pub fun make() -> IEntity {
        return heap Line() as IEntity
    }
}
