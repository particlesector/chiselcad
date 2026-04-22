// if/else control flow test — V2 Tier 2b
// Each scene is a translate() block; comment out all but one to isolate.
$fn = 32;

// ─── Scene 1 (0, 0, 0): if true → sphere visible ─────────────────────────────
show_sphere = 1;
translate([0, 0, 0])
    if (show_sphere) sphere(r = 6);

// ─── Scene 2 (20, 0, 0): if false → nothing rendered ─────────────────────────
translate([20, 0, 0])
    if (0) sphere(r = 6);

// ─── Scene 3 (40, 0, 0): if/else — variable selects shape ────────────────────
use_cube = 0;
translate([40, 0, 0])
    if (use_cube) cube([10, 10, 10], center = true);
    else          sphere(r = 6);

// ─── Scene 4 (0, -20, 0): expression condition ───────────────────────────────
r = 8;
translate([0, -20, 0])
    if (r > 5) sphere(r = r);
    else       cube([r, r, r], center = true);

// ─── Scene 5 (20, -20, 0): chained if/else if/else ───────────────────────────
mode = 2;
translate([20, -20, 0])
    if (mode == 1)      cube([8, 8, 8], center = true);
    else if (mode == 2) sphere(r = 5);
    else                cylinder(h = 10, r = 3, center = true);

// ─── Scene 6 (40, -20, 0): if wrapping a boolean op ─────────────────────────
hollow = 1;
translate([40, -20, 0])
    if (hollow)
        difference() {
            cube([12, 12, 12], center = true);
            sphere(r = 7);
        }
    else
        cube([12, 12, 12], center = true);
