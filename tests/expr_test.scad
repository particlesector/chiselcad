// Expression system test — V2 Tier 2a
// Each scene is a translate() block; comment out all but one to isolate.
$fn = 48;

// ─── Scene 1 (0, 0, 0): Variable in sphere radius ────────────────────────────
// r = 8; sphere(r = r)  — should look identical to sphere(r=8)
r = 8;
translate([0, 0, 0])
    sphere(r = r);

// ─── Scene 2 (25, 0, 0): Arithmetic in params ────────────────────────────────
// base = 5; cube([base*2, base*3, base])  → cube([10, 15, 5])
base = 5;
translate([25, 0, 0])
    cube([base*2, base*3, base]);

// ─── Scene 3 (55, 0, 0): Expression in translate vector ──────────────────────
// gap = 12; sphere at translate([gap + 3, 0, 0]) → translate([15, 0, 0])
gap = 12;
translate([gap + 3, 55, 0])
    sphere(r = 5);

// ─── Scene 4 (0, -30, 0): Trig in transform — rotated cylinder ───────────────
// angle = 30; translate([sin(angle)*15, cos(angle)*15, 0]) cylinder
angle = 30;
translate([sin(angle)*15, cos(angle)*15 - 30, 0])
    cylinder(h = 10, r = 3, center = true);

// ─── Scene 5 (35, -30, 0): Math built-ins in params ──────────────────────────
// sqrt(144) = 12; sphere(r = sqrt(144)/2) → sphere(r=6)
translate([35, -30, 0])
    sphere(r = sqrt(144) / 2);

// ─── Scene 6 (-30, 0, 0): Variable shared across translate and primitive ──────
// size = 10; translate([size, 0, 0]) cube([size, size, size])
// The cube should sit exactly one cube-width to the right of origin
size = 10;
translate([-30 + size, 0, 0])
    cube([size, size, size]);

// ─── Scene 7 (-30, -30, 0): Nested arithmetic ────────────────────────────────
// w = 4; h = w * w / 2;  cylinder(h = h, r = w)  → cylinder(h=8, r=4)
w = 4;
h = w * w / 2;
translate([-30, -30, 0])
    cylinder(h = h, r = w, center = true);

// ─── Scene 8 (0, 30, 0): Variable-driven difference ──────────────────────────
// outer = 10; inner = outer - 3;  difference cube minus sphere
outer = 10;
inner = outer - 3;
translate([0, 30, 0])
    difference() {
        cube([outer*2, outer*2, outer*2], center = true);
        sphere(r = outer + inner);
    }
