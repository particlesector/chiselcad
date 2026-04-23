// module test — V2 Tier 3
// Each scene is a translate() block; comment out all but one to isolate.
$fn = 32;

// ─── Scene 1 (0, 0, 0): simple parameterized sphere module ────────────────
module ball(r) {
    sphere(r = r);
}
translate([0, 0, 0])
    ball(5);

// ─── Scene 2 (25, 0, 0): module with default parameter ────────────────────
module disk(r, h = 3) {
    cylinder(r = r, h = h, center = true);
}
translate([25, 0, 0])
    disk(r = 6);

// ─── Scene 3 (55, 0, 0): module called with all params ────────────────────
translate([55, 0, 0])
    disk(r = 4, h = 8);

// ─── Scene 4 (85, 0, 0): module with multiple primitives (union) ──────────
module capsule(r, h) {
    cylinder(r = r, h = h, center = true);
    translate([0, 0,  h/2]) sphere(r = r);
    translate([0, 0, -h/2]) sphere(r = r);
}
translate([85, 0, 0])
    capsule(r = 3, h = 10);

// ─── Scene 5 (0, -40, 0): module called in a for loop ─────────────────────
module dot(r) {
    sphere(r = r);
}
translate([0, -40, 0])
    for (i = [0:4])
        translate([i * 12, 0, 0]) dot(i + 1);

// ─── Scene 6 (0, -80, 0): module using an expression as argument ───────────
base = 4;
translate([0, -80, 0])
    for (i = [0:3])
        translate([i * 14, 0, 0]) disk(r = base + i, h = (i + 1) * 2);

// ─── Scene 7 (0, -120, 0): rounded_box — minkowski of cube + sphere ────────
module rounded_box(w, h, d, r) {
    minkowski() {
        cube([w - r*2, h - r*2, d - r*2], center = true);
        sphere(r = r);
    }
}
translate([0, -120, 0])
    rounded_box(w = 20, h = 12, d = 8, r = 2);

// ─── Scene 8 (40, -120, 0): module env isolation — caller r unchanged ──────
r = 99;
translate([40, -120, 0])
    ball(6);
// sphere at (65,-120,0) should use r=99 (caller's env restored after ball())
translate([65, -120, 0])
    sphere(r = r);
