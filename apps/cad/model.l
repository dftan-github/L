// CAD drawing model — the scene, its entities, and the camera-free view math.
//
// The shapes themselves live in the waf.entity.* files:
//   entity.l        — IEntity, the shared interface
//   entity.pos.l    — Pos     (a free point)
//   entity.line.l   — Line    (two point components)
//   entity.circle.l — Circle  (centre + rim components)
// plus scene.l, the non-owning draw index.
//
// What is left here is the one number the geometry needs and nothing else
// provides: a square root. The camera (pan/zoom) moved to Canvas, because it is
// a view concern and the scene is only the list of entities (WAF: CCanvas owns
// the camera, CScene does not).

// Source distance. Newton iteration on the L side — the runtime exposes no
// sqrt, and adding a C primitive for a demo alone would not earn its keep.
//
// NOTE: entity.l has its own `dist(dx, dy)` for the same reason; the two are
// deliberately the same function and must stay numerically identical.
pub fun cad_dist(f64 dx, f64 dy) -> f64 {
    d := dx * dx + dy * dy
    if d <= 0.0 {
        return 0.0
    }
    x := d
    i! := 0
    while i < 24 {
        x = 0.5 * (x + d / x)
        i! = i + 1
    }
    return x
}
