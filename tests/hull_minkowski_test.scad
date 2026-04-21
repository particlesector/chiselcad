// hull() and minkowski() test file — V2 first ops
// Load this file in ChiselCAD to visually verify the new operations.
$fn = 32;

// ---------------------------------------------------------------------------
// 1. hull() — two spheres at opposite ends produce a capsule shape
// ---------------------------------------------------------------------------
translate([-30, 0, 0])
hull() {
    translate([-8, 0, 0]) sphere(r = 4);
    translate([ 8, 0, 0]) sphere(r = 4);
}

// ---------------------------------------------------------------------------
// 2. hull() — three cylinders at triangle vertices produce a rounded triangle
// ---------------------------------------------------------------------------
translate([0, 0, 0])
hull() {
    translate([ 0,  10, 0]) cylinder(h = 8, r = 3, center = true);
    translate([-9,  -5, 0]) cylinder(h = 8, r = 3, center = true);
    translate([ 9,  -5, 0]) cylinder(h = 8, r = 3, center = true);
}

// ---------------------------------------------------------------------------
// 3. hull() — sphere + cube produces a rounded corner box
// ---------------------------------------------------------------------------
translate([30, 0, 0])
hull() {
    cube([12, 12, 12], center = true);
    sphere(r = 8);
}

// ---------------------------------------------------------------------------
// 4. minkowski() — cube + sphere produces a box with rounded corners/edges
// ---------------------------------------------------------------------------
translate([0, -35, 0])
minkowski() {
    cube([14, 10, 6], center = true);
    sphere(r = 3);
}

// ---------------------------------------------------------------------------
// 5. minkowski() — cylinder + sphere produces a rounded disc / pill
// ---------------------------------------------------------------------------
translate([35, -35, 0])
minkowski() {
    cylinder(h = 2, r = 8, center = true);
    sphere(r = 2);
}

// ---------------------------------------------------------------------------
// 6. hull() inside difference() — hull acts as the cutting tool
// ---------------------------------------------------------------------------
translate([-30, -35, 0])
difference() {
    cube([18, 18, 18], center = true);
    hull() {
        translate([-4, 0, 0]) sphere(r = 4);
        translate([ 4, 0, 0]) sphere(r = 4);
    }
}
