// for loop test — V2 Tier 2c
// Each scene is a translate() block; comment out all but one to isolate.
$fn = 32;

// ─── Scene 1 (0, 0, 0): simple range — row of spheres ────────────────────────
// for (i = [0:4]) — five spheres spaced 12 apart
translate([0, 0, 0])
    for (i = [0:4])
        translate([i*12, 0, 0]) sphere(r=4);

// ─── Scene 2 (0, -25, 0): range with step — every other position ─────────────
// for (i = [0:2:8]) — five spheres at i=0,2,4,6,8
translate([0, -25, 0])
    for (i = [0:2:8])
        translate([i*6, 0, 0]) sphere(r=3);

// ─── Scene 3 (0, -50, 0): list of values ─────────────────────────────────────
// Cylinders of varying radii
translate([0, -50, 0])
    for (r = [2, 4, 6, 4, 2])
        translate([r*5, 0, 0]) cylinder(h=r*2, r=r, center=true);

// ─── Scene 4 (0, -80, 0): loop variable in expression ────────────────────────
// Staircase — cube height grows with i
translate([0, -80, 0])
    for (i = [0:5])
        translate([i*8, 0, (i+1)*2]) cube([6, 6, (i+1)*4], center=true);

// ─── Scene 5 (0, -115, 0): for inside difference (cutting pattern) ───────────
// Cube with a row of cylindrical holes
translate([0, -115, 0])
    difference() {
        cube([60, 12, 12], center=true);
        for (i = [0:4])
            translate([-24 + i*12, 0, 0]) cylinder(h=20, r=3, center=true);
    }

// ─── Scene 6 (0, -145, 0): nested for — grid of spheres ─────────────────────
translate([0, -145, 0])
    for (x = [0:3])
        for (y = [0:3])
            translate([x*10, y*10, 0]) sphere(r=3);
