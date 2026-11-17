// waf.entity.component — the two entity containers, each embedded by `delegate`.
//
//   CEntity   — the *inputs* of one shape (a line's endpoints, a circle's
//               centre + rim). Every concrete entity embeds it:
//
//                 pub class Line : IEntity {
//                     delegate CEntity
//                     ...
//                 }
//
//   CEntitys  — a flat *set* of entities in draw order. What the scene is made
//               of, and what the command tree holds:
//
//                 pub class Scene {
//                     delegate CEntitys
//                 }
//
// A shape is not a bag of private coordinates: it is a set of inputs, and each
// input is itself an entity (a point). Sharing that point between a line and a
// circle is what makes dragging one grip move both. CEntity is merely the
// storage for that; deriving a shape's own geometry (midpoint, radius) stays in
// the shape, because only it knows what its inputs mean.

import "stdlib/memory.l"
import "stdlib/array.l"
import "apps/cad/entity.l"

pub class CEntity {
    Array<IEntity> components

    pub fun init() {
        this.components = Array<IEntity>()
    }

    pub fun component_count() -> i32 {
        return this.components.len()
    }

    pub fun component_at(i32 i) -> IEntity {
        return this.components.get(i)
    }

    // Attaches one input. Heap cells only: a stack temporary would die with the
    // caller's frame while the shape still refers to it.
    pub fun attach(IEntity p) {
        this.components.push(p)
    }

    pub fun detach_all() {
        this.components.clear()
    }

    // Brings every input up to date. False when an input is not ready, which
    // aborts the shape's rebuild (CEntity::update's first half).
    pub fun push() -> bool {
        i32 i = 0
        while i < this.components.len() {
            IEntity c = this.components.get(i)
            if !c.update() {
                return false
            }
            i = i + 1
        }
        return true
    }
}

pub class CEntitys {
    Array<IEntity> items

    pub fun init() {
        this.items = Array<IEntity>()
    }

    pub fun len() -> i32 {
        return this.items.len()
    }

    pub fun get(i32 i) -> IEntity {
        return this.items.get(i)
    }

    // Replaces the entity in an existing slot. The command layer collects picks
    // this way: a pick is not a shared point yet, so filling slot `i` in place
    // keeps the list at `needed()` entries and never grows it.
    pub fun set(i32 i, IEntity e) {
        if i < 0 || i >= this.items.len() {
            return
        }
        this.items.set(i, e)
    }

    // Non-owning: the container is a draw index, so adding never copies and
    // the entity must already live on the heap.
    pub fun add(IEntity e) {
        this.items.push(e)
    }

    pub fun clear_all() {
        this.items.clear()
    }

    // Drops the slot holding `e`. eids are unique, so the first match is the
    // only one — removing it and returning keeps this O(n) with no wasted pass
    // over the elements behind it. A staging array would be worse than this
    // either way: a local `Array<IEntity>` auto-frees its elements on scope
    // exit, and those elements are entities this container only borrows.
    pub fun remove(IEntity e) {
        i32 i = 0
        while i < this.items.len() {
            if same_entity(this.items.get(i), e) {
                this.items.remove(i)
                return
            }
            i = i + 1
        }
    }

    pub fun contains(IEntity e) -> bool {
        i32 i = 0
        while i < this.items.len() {
            if same_entity(this.items.get(i), e) {
                return true
            }
            i = i + 1
        }
        return false
    }
}
