// hull() and minkowski() visual test — V2
// To test one scene at a time, comment out all other translate() blocks below.
$fn = 32;

// ---------------------------------------------------------------------------
// Scene 1: hull() — two spheres → capsule
// ---------------------------------------------------------------------------
translate([-30, 0, 0])
hull() {
    translate([-8, 0, 0]) sphere(r = 4);
    translate([ 8, 0, 0]) sphere(r = 4);
}

// ---------------------------------------------------------------------------
// Scene 2: hull() — three cylinders at triangle vertices → rounded triangle
// ---------------------------------------------------------------------------
translate([0, 0, 0])
hull() {
    translate([ 0,  10, 0]) cylinder(h = 8, r = 3, center = true);
    translate([-9,  -5, 0]) cylinder(h = 8, r = 3, center = true);
    translate([ 9,  -5, 0]) cylinder(h = 8, r = 3, center = true);
}

// ---------------------------------------------------------------------------
// Scene 3: hull() — cube + sphere → rounded corner blob
// ---------------------------------------------------------------------------
translate([30, 0, 0])
hull() {
    cube([12, 12, 12], center = true);
    sphere(r = 8);
}

// ---------------------------------------------------------------------------
// Scene 4: minkowski() — cube + sphere → box with fully rounded edges
// ---------------------------------------------------------------------------
translate([0, -35, 0])
minkowski() {
    cube([14, 10, 6], center = true);
    sphere(r = 3);
}

// ---------------------------------------------------------------------------
// Scene 5: minkowski() — cylinder + sphere → rounded disc / pill
// ---------------------------------------------------------------------------
translate([35, -35, 0])
minkowski() {
    cylinder(h = 2, r = 8, center = true);
    sphere(r = 2);
}

// ---------------------------------------------------------------------------
// Scene 6: hull() inside difference() — hull used as cutting tool
// ---------------------------------------------------------------------------
translate([-30, -35, 0])
difference() {
    cube([18, 18, 18], center = true);
    hull() {
        translate([-4, 0, 0]) sphere(r = 4);
        translate([ 4, 0, 0]) sphere(r = 4);
    }
}
