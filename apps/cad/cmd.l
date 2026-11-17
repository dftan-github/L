// Draw commands, modelled on WAF's CCmdEntity.
//
// Each concrete command lives in its own file next to the entity it builds:
//   cmd.entity.pos.l    — CmdEntityPos    (one pick)
//   cmd.entity.line.l   — CmdEntityLine   (two picks)
//   cmd.entity.circle.l — CmdEntityCircle (two picks)
//
// A command owns its collected picks and mints the entity they describe. To the
// app they are one thing, so they sit in a single list keyed by their own
// `kind`: adding a fourth command is one `register` line, not a new field, a
// new accessor and four new branches. `Cmd` tracks only which kind is running.
//
// NOTE: inside a method, class fields are only reachable through `this.`.

import "stdlib/memory.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "apps/cad/entity.l"
import "apps/cad/entity.pos.l"
import "apps/cad/entity.line.l"
import "apps/cad/entity.circle.l"
import "apps/cad/scene.l"
import "apps/cad/cmd.entity.pos.l"
import "apps/cad/cmd.entity.line.l"
import "apps/cad/cmd.entity.circle.l"

// What the command tree and the UI need from any drawing command. Picks stay
// private to the command; callers read them back through px/py.
pub interface ICmd {
    fun needed() -> i32
    fun count() -> i32
    fun px(i32 i) -> f64
    fun py(i32 i) -> f64
    fun push(f64 x, f64 y)
    fun clear()
    fun ready() -> bool
    fun kind() -> i32
    fun kind_name() -> String
    // The empty entity this command builds; commit links the picks into it.
    fun make() -> IEntity
}

pub class Cmd {
    Array<ICmd> commands
    // The running kind (0 = idle) and its command, so a click does not re-look-up.
    i32 kind
    ICmd active

    pub fun init() {
        this.commands = Array<ICmd>()
        this.register(CmdEntityPos() as ICmd)
        this.register(CmdEntityLine() as ICmd)
        this.register(CmdEntityCircle() as ICmd)
        this.kind = 0
        this.active = this.commands.get(0)
    }

    pub fun register(ICmd c) {
        this.commands.push(c)
    }

    // The command for `k`. An unknown kind is an empty command (count 0), so
    // callers need no Optional and no branch.
    pub fun get(i32 k) -> ICmd {
        i32 i = 0
        while i < this.commands.len() {
            ICmd c = this.commands.get(i)
            if k == c.kind() {
                return c
            }
            i = i + 1
        }
        return this.commands.get(0)
    }

    pub fun current() -> ICmd {
        return this.active
    }

    pub fun kind_name() -> String {
        if this.is_idle() {
            return String("SELECT")
        }
        return this.active.kind_name()
    }

    pub fun is_idle() -> bool {
        return 0 == this.kind
    }

    pub fun count() -> i32 {
        return this.active.count()
    }

    pub fun needed() -> i32 {
        return this.active.needed()
    }

    pub fun px(i32 i) -> f64 {
        return this.active.px(i)
    }

    pub fun py(i32 i) -> f64 {
        return this.active.py(i)
    }

    pub fun ready() -> bool {
        return this.active.ready()
    }

    // Drops every pick of every command and returns to idle.
    pub fun cancel() {
        this.kind = 0
        i32 i = 0
        while i < this.commands.len() {
            this.commands.get(i).clear()
            i = i + 1
        }
    }

    // Enters command `k`, starting with an empty pick list.
    pub fun begin(i32 k) {
        this.cancel()
        this.kind = k
        this.active = this.get(k)
    }

    pub fun push_pick(f64 x, f64 y) {
        this.active.push(x, y)
    }

    // Builds the entity from the picks and hands it to the scene. True when
    // something was added.
    pub fun commit(Scene s!) -> bool {
        if !this.ready() {
            return false
        }
        ICmd cmd = this.active
        IEntity e = cmd.make()
        // Feed only the slots the minted entity declares: a point takes one, a
        // line or circle two. Asking a one-pick command for px(1) would read
        // past its collected picks.
        i32 slots = e.slot_count()
        f64 ax = cmd.px(0)
        f64 ay = cmd.py(0)
        f64 bx = 0.0
        f64 by = 0.0
        if slots > 1 {
            bx = cmd.px(1)
            by = cmd.py(1)
        }
        e.set_points(ax, ay, bx, by)
        e.update()
        s.add(e)
        return true
    }
}
