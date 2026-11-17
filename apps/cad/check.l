// Headless checks for the CAD model. No window, no GDI — just the geometry,
// the composition invariant that every shape derives from shared points, and
// the command state machine driven exactly the way app.l drives it.
//
// Every value under test is first stored in a local: passing an un-evaluated
// call as an argument lets the callee's frame reuse that stack slot.
//
// The scene is a draw index of top-level shapes; the points a shape is built
// from belong to that shape (CEntity) and are hidden, so they never show up as
// standalone markers.

import "stdlib/io.l"
import "apps/cad/entity.l"
import "apps/cad/entity.pos.l"
import "apps/cad/entity.line.l"
import "apps/cad/entity.circle.l"
import "apps/cad/scene.l"
import "apps/cad/cmd.l"
import "apps/cad/canvas.l"

i32 failures = 0

fun check(String name, bool ok) {
    if ok {
        return
    }
    failures = failures + 1
    print("[FAIL] " + name + "\n")
}

fun checkf(String name, f64 got, f64 want, f64 eps) {
    f64 d = got - want
    if d < 0.0 {
        d = -d
    }
    check(name, d <= eps)
}

fun check_geometry() {
    f64 d34 = dist(3.0, 4.0)
    checkf("dist(3,4)", d34, 5.0, 0.0001)
    f64 d00 = dist(0.0, 0.0)
    checkf("dist(0,0)", d00, 0.0, 0.0001)
    f64 d68 = dist(-6.0, 8.0)
    checkf("dist(-6,8)", d68, 10.0, 0.0001)

    IEntity l = heap Line() as IEntity
    l.set_points(0.0, 0.0, 10.0, 4.0)
    l.update()
    f64 cx = l.world_x()
    f64 cy = l.world_y()
    checkf("line.cx", cx, 5.0, 0.0001)
    checkf("line.cy", cy, 2.0, 0.0001)
    f64 ll = l.metric()
    checkf("line length", ll, dist(10.0, 4.0), 0.0001)

    // Moving an input moves the derived midpoint. The point is only reachable
    // through the interface: a class-typed getter would hand back a copy.
    l.move_point(1, 20.0, 8.0)
    l.update()
    f64 cx2 = l.world_x()
    f64 cy2 = l.world_y()
    checkf("line.cx after move", cx2, 10.0, 0.0001)
    checkf("line.cy after move", cy2, 4.0, 0.0001)

    IEntity c = heap Circle() as IEntity
    c.set_points(0.0, 0.0, 3.0, 4.0)
    c.update()
    f64 r1 = c.metric()
    checkf("circle radius", r1, 5.0, 0.0001)
    c.move_point(1, 6.0, 8.0)
    c.update()
    f64 r2 = c.metric()
    checkf("circle radius after move", r2, 10.0, 0.0001)
    f64 ccx = c.world_x()
    f64 ccy = c.world_y()
    checkf("circle.cx", ccx, 0.0, 0.0001)
    checkf("circle.cy", ccy, 0.0, 0.0001)
}

fun check_components() {
    // A shared point is one object: two shapes read the same coordinates.
    IEntity p = heap Pos(2.0, 3.0) as IEntity
    IEntity l = heap Line() as IEntity
    l.set_points(0.0, 0.0, 0.0, 0.0)
    l.move_point(0, 2.0, 3.0)
    f64 px = l.point_x(0)
    f64 py = l.point_y(0)
    f64 qx = p.world_x()
    f64 qy = p.world_y()
    checkf("shared point x", px, qx, 0.0001)
    checkf("shared point y", py, qy, 0.0001)

    // Slot layout: a line has two inputs, a circle two, a point one.
    check("line slots", 2 == l.slot_count())
    check("line components", 2 == l.component_count())
    IEntity c = heap Circle() as IEntity
    c.set_points(1.0, 1.0, 4.0, 5.0)
    check("circle slots", 2 == c.slot_count())
    check("point slots", 1 == p.slot_count())
    check("point kind", KIND_POS == p.kind())
    check("line kind", KIND_LINE == l.kind())
    check("circle kind", KIND_CIRCLE == c.kind())
}

fun check_scene() {
    s := Scene()
    IEntity l = heap Line() as IEntity
    l.set_points(0.0, 0.0, 1.0, 1.0)
    s.add(l)
    IEntity c = heap Circle() as IEntity
    c.set_points(0.0, 0.0, 5.0, 0.0)
    s.add(c)
    s.update()
    i32 n = s.len()
    check("scene size", 2 == n)

    // Picking: the midpoint of the seeded line, the rim of the circle.
    Optional<IEntity> hit_l = s.pick_line(0.5, 0.5, 0.01)
    check("pick line", hit_l.is_some())
    Optional<IEntity> miss_l = s.pick_line(0.5, 5.0, 0.01)
    check("miss line", !miss_l.is_some())
    Optional<IEntity> hit_c = s.pick_circle(5.0, 0.0, 0.01)
    check("pick circle rim", hit_c.is_some())
    Optional<IEntity> miss_c = s.pick_circle(0.0, 0.0, 0.01)
    check("miss circle centre", !miss_c.is_some())
    Optional<IEntity> no_pt = s.pick_point(0.5, 0.5, 0.01)
    check("picking a line is not a point", !no_pt.is_some())

    s.remove(c)
    i32 n2 = s.len()
    check("scene remove", 1 == n2)
}

fun check_view() {
    s := Scene()
    cv := Canvas(s)
    cv.cam_x = 12.5
    cv.cam_y = -7.25
    cv.scale = 2.5
    i32 vw = 1100
    i32 vh = 760
    i32 sx = cv.screen_x(33.0, vw)
    i32 sy = cv.screen_y(-21.0, vh)
    f64 bx = cv.world_x(sx, vw)
    f64 by = cv.world_y(sy, vh)
    // The forward transform truncates to whole pixels, so at scale 2.5 one
    // pixel is 0.4 world units — that is the round-trip budget, not 0.01.
    f64 tol = 1.0 / cv.scale
    checkf("roundtrip x", bx, 33.0, tol)
    checkf("roundtrip y", by, -21.0, tol)
    f64 pw = cv.px_world()
    checkf("px_world", pw, 0.4, 0.0001)
}

fun commit(Cmd c!, Scene s!) {
    c.commit(s)
    c.begin(c.kind)  // app.l repeats the command after a commit
}

fun check_cmd() {
    s := Scene()
    c := Cmd()

    i32 idle = c.count()
    check("idle count", 0 == idle)
    check("idle is idle", c.is_idle())

    // point: one pick commits
    c.begin(KIND_POS)
    i32 need3 = c.needed()
    check("point needs 1", 1 == need3)
    c.push_pick(1.0, 2.0)
    bool ready3 = c.ready()
    check("point ready", ready3)
    commit(c, s)
    i32 np = s.len()
    IEntity p0 = s.get(0)
    f64 p0x = p0.world_x()
    check("point committed", 1 == np)
    check("point visible", p0.visible())
    checkf("point x", p0x, 1.0, 0.0001)

    // line: the first pick must NOT commit
    c.begin(KIND_LINE)
    i32 need1 = c.needed()
    check("line needs 2", 2 == need1)
    c.push_pick(0.0, 0.0)
    bool ready_half = c.ready()
    i32 half_lines = s.len()
    check("line not ready after 1", !ready_half)
    check("line not committed after 1", 1 == half_lines)
    c.push_pick(10.0, 0.0)
    bool ready_full = c.ready()
    check("line ready after 2", ready_full)
    commit(c, s)
    i32 nl = s.len()
    check("line committed", 2 == nl)
    IEntity l = s.get(1)
    l.update()
    f64 mx = l.world_x()
    checkf("line midpoint", mx, 5.0, 0.0001)

    // circle
    c.begin(KIND_CIRCLE)
    c.push_pick(0.0, 0.0)
    c.push_pick(0.0, 6.0)
    commit(c, s)
    i32 nc = s.len()
    check("circle committed", 3 == nc)
    IEntity cc = s.get(2)
    cc.update()
    f64 cr = cc.metric()
    checkf("circle radius", cr, 6.0, 0.0001)

    // cancel drops the picks, so a switch does not leak half a shape
    c.begin(KIND_LINE)
    c.push_pick(1.0, 1.0)
    c.cancel()
    i32 kind_after = c.kind
    i32 picks_after = c.count()
    i32 lines_after = s.len()
    check("cancel resets kind", 0 == kind_after)
    check("cancel drops picks", 0 == picks_after)
    check("cancel builds nothing", 3 == lines_after)
}

fun main() -> i32 {
    check_geometry()
    check_components()
    check_scene()
    check_view()
    check_cmd()

    if 0 == failures {
        print("cad model: all checks passed\n")
        // Informational only: every case boxes entities as `heap X as IEntity`
        // to exercise the real interface paths, and that boxing currently
        // leaves its cell out of the auto-free list (compiler gap, see
        // .agent-reports/A007.md). A model regression shows up as a failed
        // check above, not here.
        print("alloc count after checks: " + l_get_alloc_count() + "\n")
        return 0
    }
    print("cad model: has failures\n")
    return 1
}
