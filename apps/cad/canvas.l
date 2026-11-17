// Canvas — the drawing surface and the camera that maps world to pixels.
//
// The canvas owns nothing but the camera: it borrows the scene and redraws it
// every frame from the current camera. That keeps pan/zoom a pure camera change
// with no invalidation bookkeeping (WAF: CCanvas owns the camera, CScene is
// just the list of entities).
//
// Selection and hover are tracked by entity id, not by pool index: the scene is
// polymorphic now, so there is no "line pool" index to speak of. Comparing eids
// is O(entities) per draw, which is nothing at this size and keeps the marks
// valid when entities are added or removed between frames.
//
// NOTE: inside a method, class fields are only reachable through `this.`.

import "stdlib/win32.l"
import "stdlib/string.l"
import "stdlib/memory.l"
import "apps/cad/entity.l"
import "apps/cad/scene.l"
import "apps/cad/cmd.l"
import "apps/cad/model.l"

// Palette. Kept in one place so the demo reads as a coherent UI.
const COL_BG_R = 22
const COL_BG_G = 24
const COL_BG_B = 28
const COL_GRID_MINOR_R = 44
const COL_GRID_MINOR_G = 48
const COL_GRID_MINOR_B = 56
const COL_GRID_MAJOR_R = 64
const COL_GRID_MAJOR_G = 70
const COL_GRID_MAJOR_B = 82
const COL_AXIS_R = 96
const COL_AXIS_G = 106
const COL_AXIS_B = 124
const COL_ENT_R = 210
const COL_ENT_G = 214
const COL_ENT_B = 222
const COL_SEL_R = 255
const COL_SEL_G = 96
const COL_SEL_B = 84
const COL_HOT_R = 255
const COL_HOT_G = 196
const COL_HOT_B = 64
const COL_PT_R = 130
const COL_PT_G = 200
const COL_PT_B = 255
const COL_HUD_R = 180
const COL_HUD_G = 186
const COL_HUD_B = 196

// Grid spacing in world millimetres; every fifth line is drawn brighter.
const GRID_MM = 10
const GRID_MAJOR_EVERY = 5

// A cross marks a free point; its arm is a fixed pixel size so it stays
// clickable at any zoom (WAF: CPos.arm_px).
const PT_ARM = 5

pub class Canvas {
    Scene scene
    // Camera. World → screen is (world - cam) * scale + viewport centre, y up.
    f64 cam_x
    f64 cam_y
    f64 scale
    // Marks, by entity id. 0 means "nothing".
    i64 sel_eid
    i64 hot_eid

    pub fun init(Scene s) {
        this.scene = s
        this.cam_x = 0.0
        this.cam_y = 0.0
        this.scale = 3.0
        this.sel_eid = 0
        this.hot_eid = 0
    }

    // ---- view transform ------------------------------------------------

    pub fun screen_x(f64 wx, i32 vw) -> i32 {
        return l_f64_to_i32((wx - this.cam_x) * this.scale + vw * 0.5)
    }

    pub fun screen_y(f64 wy, i32 vh) -> i32 {
        return l_f64_to_i32(vh * 0.5 - (wy - this.cam_y) * this.scale)
    }

    pub fun world_x(i32 sx, i32 vw) -> f64 {
        return (sx - vw * 0.5) / this.scale + this.cam_x
    }

    pub fun world_y(i32 sy, i32 vh) -> f64 {
        return (vh * 0.5 - sy) / this.scale + this.cam_y
    }

    // World units covered by one screen pixel — the pick tolerance.
    pub fun px_world() -> f64 {
        return 1.0 / this.scale
    }

    pub fun clear_marks() {
        this.sel_eid = 0
        this.hot_eid = 0
    }

    pub fun clear_hover() {
        this.hot_eid = 0
    }

    pub fun select(IEntity e) {
        this.sel_eid = e.eid()
    }

    pub fun hover(IEntity e) {
        this.hot_eid = e.eid()
    }

    // ---- camera --------------------------------------------------------

    // Applies a zoom step about the cursor so the world point under it stays put.
    pub fun zoom(i32 mx, i32 my, i32 vw, i32 vh, bool zoom_in) {
        f64 bw = this.world_x(mx, vw)
        f64 bh = this.world_y(my, vh)
        f64 sc = this.scale
        if zoom_in {
            sc = sc * 1.15
        } else {
            sc = sc / 1.15
        }
        if sc < 0.05 {
            sc = 0.05
        }
        if sc > 40.0 {
            sc = 40.0
        }
        this.scale = sc
        f64 aw = this.world_x(mx, vw)
        f64 ah = this.world_y(my, vh)
        this.cam_x = this.cam_x + (bw - aw)
        this.cam_y = this.cam_y + (bh - ah)
    }

    pub fun pan(i32 dx, i32 dy) {
        this.cam_x = this.cam_x - dx / this.scale
        this.cam_y = this.cam_y + dy / this.scale
    }

    // ---- layers --------------------------------------------------------

    pub fun draw_grid(i32 vw, i32 vh) {
        f64 spacing = GRID_MM * 1.0
        f64 left = this.world_x(0, vw)
        f64 right = this.world_x(vw, vw)
        f64 top = this.world_y(0, vh)
        f64 bottom = this.world_y(vh, vh)

        i32 i = l_f64_to_i32(left / spacing) - 1
        i32 i1 = l_f64_to_i32(right / spacing) + 1
        while i <= i1 {
            i32 sx = this.screen_x(i * spacing, vw)
            i32 r = COL_GRID_MINOR_R
            i32 g = COL_GRID_MINOR_G
            i32 b = COL_GRID_MINOR_B
            if i % GRID_MAJOR_EVERY == 0 {
                r = COL_GRID_MAJOR_R
                g = COL_GRID_MAJOR_G
                b = COL_GRID_MAJOR_B
            }
            if i == 0 {
                r = COL_AXIS_R
                g = COL_AXIS_G
                b = COL_AXIS_B
            }
            vline(sx, 0, vh, r, g, b)
            i = i + 1
        }

        i32 j = l_f64_to_i32(bottom / spacing) - 1
        i32 j1 = l_f64_to_i32(top / spacing) + 1
        while j <= j1 {
            i32 sy = this.screen_y(j * spacing, vh)
            i32 r = COL_GRID_MINOR_R
            i32 g = COL_GRID_MINOR_G
            i32 b = COL_GRID_MINOR_B
            if j % GRID_MAJOR_EVERY == 0 {
                r = COL_GRID_MAJOR_R
                g = COL_GRID_MAJOR_G
                b = COL_GRID_MAJOR_B
            }
            if j == 0 {
                r = COL_AXIS_R
                g = COL_AXIS_G
                b = COL_AXIS_B
            }
            hline(0, sy, vw, r, g, b)
            j = j + 1
        }
    }

    // Recolour of an entity under the selection / hover flags.
    fun col_r(i32 sel, i32 hot, i32 base) -> i32 {
        if sel != 0 {
            return COL_SEL_R
        }
        if hot != 0 {
            return COL_HOT_R
        }
        return base
    }

    fun col_g(i32 sel, i32 hot, i32 base) -> i32 {
        if sel != 0 {
            return COL_SEL_G
        }
        if hot != 0 {
            return COL_HOT_G
        }
        return base
    }

    fun col_b(i32 sel, i32 hot, i32 base) -> i32 {
        if sel != 0 {
            return COL_SEL_B
        }
        if hot != 0 {
            return COL_HOT_B
        }
        return base
    }

    fun mark(f64 wx, f64 wy, i32 vw, i32 vh, i32 r, i32 g, i32 b) {
        i32 sx = this.screen_x(wx, vw)
        i32 sy = this.screen_y(wy, vh)
        line(sx - PT_ARM, sy, sx + PT_ARM, sy, r, g, b)
        line(sx, sy - PT_ARM, sx, sy + PT_ARM, r, g, b)
    }

    // Every entity draws itself through the same three shapes; `kind()` picks
    // which one. The concrete class is only needed for a circle's centre and
    // radius, which the interface already exposes as world_x/world_y/metric.
    pub fun draw_entities(i32 vw, i32 vh) {
        i32 n = this.scene.len()
        i32 i = 0
        while i < n {
            IEntity e = this.scene.get(i)
            if e.visible() {
                i32 sr = 0
                i32 hr = 0
                if e.eid() == this.sel_eid {
                    sr = 1
                }
                if e.eid() == this.hot_eid {
                    hr = 1
                }
                this.draw_entity(e, sr, hr, vw, vh)
            }
            i = i + 1
        }
    }

    fun draw_entity(IEntity e, i32 sr, i32 hr, i32 vw, i32 vh) {
        i32 k = e.kind()
        if KIND_POS == k {
            i32 r = this.col_r(sr, hr, COL_PT_R)
            i32 g = this.col_g(sr, hr, COL_PT_G)
            i32 b = this.col_b(sr, hr, COL_PT_B)
            this.mark(e.world_x(), e.world_y(), vw, vh, r, g, b)
            return
        }
        i32 r = this.col_r(sr, hr, COL_ENT_R)
        i32 g = this.col_g(sr, hr, COL_ENT_G)
        i32 b = this.col_b(sr, hr, COL_ENT_B)
        if KIND_LINE == k {
            i32 ax = this.screen_x(e.point_x(0), vw)
            i32 ay = this.screen_y(e.point_y(0), vh)
            i32 bx = this.screen_x(e.point_x(1), vw)
            i32 by = this.screen_y(e.point_y(1), vh)
            line(ax, ay, bx, by, r, g, b)
            return
        }
        if KIND_CIRCLE == k {
            i32 rpx = l_f64_to_i32(e.metric() * this.scale)
            if rpx > 0 {
                i32 sx = this.screen_x(e.world_x(), vw)
                i32 sy = this.screen_y(e.world_y(), vh)
                ellipse(sx - rpx, sy - rpx, rpx * 2, rpx * 2, r, g, b)
            }
        }
    }

    // Rubber band: the shape the next click would create.
    pub fun draw_preview(Cmd c, i32 mx, i32 my, i32 vw, i32 vh) {
        if c.is_idle() {
            return
        }
        if 0 == c.count() {
            this.mark(this.world_x(mx, vw), this.world_y(my, vh), vw, vh,
                      COL_HOT_R, COL_HOT_G, COL_HOT_B)
            return
        }
        f64 px = c.px(0)
        f64 py = c.py(0)
        i32 ax = this.screen_x(px, vw)
        i32 ay = this.screen_y(py, vh)
        i32 k = c.kind
        if KIND_LINE == k {
            line(ax, ay, mx, my, COL_SEL_R, COL_SEL_G, COL_SEL_B)
        } else if KIND_CIRCLE == k {
            f64 wx = this.world_x(mx, vw)
            f64 wy = this.world_y(my, vh)
            i32 rpx = l_f64_to_i32(cad_dist(wx - px, wy - py) * this.scale)
            if rpx > 0 {
                ellipse(ax - rpx, ay - rpx, rpx * 2, rpx * 2, COL_SEL_R, COL_SEL_G, COL_SEL_B)
            }
        }
        // Live tip marker at the cursor (WAF: the tip point).
        this.mark(this.world_x(mx, vw), this.world_y(my, vh), vw, vh,
                  COL_HOT_R, COL_HOT_G, COL_HOT_B)
    }

    // One-line status strip along the bottom edge.
    pub fun draw_hud(i32 vw, i32 vh, String msg, i32 fnt) {
        i32 lh = line_height(fnt)
        i32 y = vh - lh - 6
        rect(0, y - 4, vw, lh + 10, 18, 20, 24)
        hline(0, y - 4, vw, 44, 48, 56)
        text(10, y, msg, COL_HUD_R, COL_HUD_G, COL_HUD_B)
    }
}
