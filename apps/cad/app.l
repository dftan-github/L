// App — window, event loop, and the little state machine that turns clicks and
// keys into command transitions (WAF: CApp).
//
// The loop is the usual split: drain events and mutate state, then draw one
// frame from the resulting state. Nothing in the event handlers draws, and
// nothing in the draw pass mutates, which is what keeps the frame consistent
// when several events land in the same pump.
//
// NOTE: inside a method, class fields are only reachable through `this.`.

import "stdlib/io.l"
import "stdlib/string.l"
import "stdlib/memory.l"
import "stdlib/win32.l"
import "apps/cad/entity.l"
import "apps/cad/entity.pos.l"
import "apps/cad/entity.line.l"
import "apps/cad/entity.circle.l"
import "apps/cad/scene.l"
import "apps/cad/model.l"
import "apps/cad/cmd.l"
import "apps/cad/canvas.l"

const WIN_W = 1100
const WIN_H = 760
const PICK_PX = 7

const VK_L = 76
const VK_C = 67
const VK_P = 80

pub class App {
    Scene scene
    Canvas canvas
    Cmd cmd
    ptr win
    i32 fnt
    i32 panning
    i32 pan_x
    i32 pan_y
    i32 running

    pub fun init() {
        Scene sc = Scene()
        // Seed with one of each kind so the window is not empty on start.
        p := heap Pos(90.0, -50.0) as IEntity
        l1 := heap Line() as IEntity
        l1.set_points(-60.0, -40.0, 60.0, 40.0)
        l2 := heap Line() as IEntity
        l2.set_points(-60.0, 40.0, -20.0, 70.0)
        c1 := heap Circle() as IEntity
        c1.set_points(0.0, 0.0, 70.0, 0.0)
        sc.add(l1)
        sc.add(c1)
        sc.add(l2)
        sc.add(p)
        this.scene = sc
        this.canvas = Canvas(sc)
        this.cmd = Cmd()
        this.win = l_null_ptr()
        this.fnt = 0
        this.panning = 0
        this.pan_x = 0
        this.pan_y = 0
        this.running = 1
    }

    pub fun open_window() -> bool {
        this.win = open("L CAD", WIN_W, WIN_H)
        if l_ptr_is_null(this.win) != 0 {
            return false
        }
        this.fnt = font("Consolas", 13, false)
        set_font(this.fnt)
        return true
    }

    // ---- picking -------------------------------------------------------
    //
    // Points win over lines, lines over circles: a grip is the smaller target,
    // so it must be reachable even when it sits on a segment.

    fun pick(f64 wx, f64 wy) {
        f64 tol = PICK_PX * this.canvas.px_world()
        Optional<IEntity> p = this.scene.pick_point(wx, wy, tol)
        if p.is_some() {
            this.canvas.select(p.unwrap())
            return
        }
        Optional<IEntity> l = this.scene.pick_line(wx, wy, tol)
        if l.is_some() {
            this.canvas.select(l.unwrap())
            return
        }
        Optional<IEntity> c = this.scene.pick_circle(wx, wy, tol)
        if c.is_some() {
            this.canvas.select(c.unwrap())
        }
    }

    fun hover(f64 wx, f64 wy) {
        f64 tol = PICK_PX * this.canvas.px_world()
        Optional<IEntity> p = this.scene.pick_point(wx, wy, tol)
        if p.is_some() {
            this.canvas.hover(p.unwrap())
            return
        }
        Optional<IEntity> l = this.scene.pick_line(wx, wy, tol)
        if l.is_some() {
            this.canvas.hover(l.unwrap())
            return
        }
        Optional<IEntity> c = this.scene.pick_circle(wx, wy, tol)
        if c.is_some() {
            this.canvas.hover(c.unwrap())
        }
    }

    // ---- events --------------------------------------------------------

    fun on_mouse_down() {
        i32 btn = event_button()
        i32 mx = event_mouse_x()
        i32 my = event_mouse_y()
        i32 vw = client_width()
        i32 vh = client_height()
        if 3 == btn {
            this.panning = 1
            this.pan_x = mx
            this.pan_y = my
            return
        }
        if 1 != btn {
            return
        }
        f64 wx = this.canvas.world_x(mx, vw)
        f64 wy = this.canvas.world_y(my, vh)
        if !this.cmd.is_idle() {
            this.cmd.push_pick(wx, wy)
            if this.cmd.ready() {
                this.cmd.commit(this.scene)
                this.cmd.begin(this.cmd.kind)  // repeat, like a CAD tool
            }
            return
        }
        this.canvas.clear_marks()
        this.pick(wx, wy)
    }

    fun on_key() {
        i32 k = event_key()
        if k == VK_ESCAPE {
            if !this.cmd.is_idle() {
                this.cmd.cancel()
            } else {
                this.running = 0
            }
            return
        }
        if k == VK_L {
            this.cmd.begin(KIND_LINE)
            return
        }
        if k == VK_C {
            this.cmd.begin(KIND_CIRCLE)
            return
        }
        if k == VK_P {
            this.cmd.begin(KIND_POS)
        }
    }

    fun handle_event() {
        i32 t = event_type()
        if t == EV_CLOSE {
            this.running = 0
            return
        }
        if t == EV_WHEEL {
            this.canvas.zoom(event_mouse_x(), event_mouse_y(), client_width(), client_height(), event_wheel() > 0)
            return
        }
        if t == EV_MOUSE_DOWN {
            this.on_mouse_down()
            return
        }
        if t == EV_MOUSE_UP {
            this.panning = 0
            return
        }
        if t == EV_MOUSE_MOVE {
            if this.panning != 0 {
                i32 mx = event_mouse_x()
                i32 my = event_mouse_y()
                this.canvas.pan(mx - this.pan_x, my - this.pan_y)
                this.pan_x = mx
                this.pan_y = my
            }
            return
        }
        if t == EV_KEY_DOWN {
            this.on_key()
        }
    }

    // ---- frame ---------------------------------------------------------

    pub fun status_text() -> String {
        if this.cmd.is_idle() {
            return String("SELECT   L line  C circle  P point   Wheel zoom   Drag pan")
        }
        return this.cmd.kind_name() + "  " + this.cmd.count() + "/" + this.cmd.needed() + "   Esc cancel"
    }

    fun draw_frame() {
        i32 vw = client_width()
        i32 vh = client_height()
        this.canvas.clear_hover()
        if this.panning == 0 {
            this.hover(this.canvas.world_x(mouse_x(), vw), this.canvas.world_y(mouse_y(), vh))
        }
        frame_begin()
        clear(COL_BG_R, COL_BG_G, COL_BG_B)
        this.canvas.draw_grid(vw, vh)
        this.canvas.draw_entities(vw, vh)
        if !this.cmd.is_idle() {
            this.canvas.draw_preview(this.cmd, mouse_x(), mouse_y(), vw, vh)
        }
        this.canvas.draw_hud(vw, vh, this.status_text(), this.fnt)
        frame_end()
    }

    pub fun run() -> i32 {
        if !this.open_window() {
            print("failed to create window\n")
            return 1
        }
        while this.running != 0 {
            if !pump() {
                break
            }
            while event_peek() {
                this.handle_event()
                event_pop()
            }
            this.draw_frame()
        }
        return 0
    }
}
