// Tier A visual test — ternary, indexing, let, user functions, concat
// Open in ChiselCAD (and optionally OpenSCAD) to compare results.

// --- Scene 1: ternary operator — cube if true, sphere if false
// Both should appear as cubes (condition is true)
translate([0, 0, 0])
    cube([true ? 8 : 4, true ? 8 : 4, true ? 8 : 4]);

translate([12, 0, 0])
    cube([false ? 8 : 4, false ? 8 : 4, false ? 8 : 4]); // smaller cube

// --- Scene 2: list indexing — extract components from a vector
// sizes = [6, 4, 10]; build a box using each component via index
sizes = [6, 4, 10];
translate([0, 16, 0])
    cube([sizes[0], sizes[1], sizes[2]]);

// --- Scene 3: let statement — scoped radius variable
// Two spheres built inside let blocks with different r values
translate([20, 16, 0])
    let(r = 5)
        sphere(r = r, $fn = 32);

translate([34, 16, 0])
    let(r = 3)
        sphere(r = r, $fn = 32);

// --- Scene 4: let expression in a parameter
// Cylinder height computed via let expression
translate([0, 32, 0])
    cylinder(h = let(base=4, factor=3) base * factor, r = 3, $fn = 24);

// --- Scene 5: user-defined function — scale factor applied to geometry
function scaled(base, factor) = base * factor;

translate([16, 32, 0])
    cube([scaled(3, 2), scaled(2, 2), scaled(1, 4)]);

// --- Scene 6: recursive function — fibonacci-like height tower
function fib(n) = n <= 1 ? n : fib(n-1) + fib(n-2);

translate([0, 50, 0])
    for (i = [1, 2, 3, 4, 5, 6])
        translate([(i-1) * 5, 0, 0])
            cylinder(h = fib(i), r = 1.5, $fn = 16);

// --- Scene 7: concat — join two lists and iterate
pts = concat([[0,0],[10,0],[10,10]], [[0,10],[5,5]]);
translate([0, 66, 0])
    for (pt = pts)
        translate([pt[0], pt[1], 0])
            cylinder(h = 3, r = 1, $fn = 12);

// --- Scene 8: ternary + variable in module param
function clamp(v, lo, hi) = v < lo ? lo : (v > hi ? hi : v);

translate([28, 50, 0]) {
    sphere(r = clamp(2, 1, 5), $fn = 24);   // 2 — within range
    translate([10, 0, 0]) sphere(r = clamp(8, 1, 5), $fn = 24);  // clamped to 5
    translate([24, 0, 0]) sphere(r = clamp(0, 1, 5), $fn = 24);  // clamped to 1
}

// --- Scene 9: for loop over vector list (now supports non-numeric values)
offsets = [[0,0,0], [8,0,0], [16,0,0], [8,8,0]];
translate([0, 82, 0])
    for (off = offsets)
        translate(off)
            cube([5, 5, 5]);

// --- Scene 10: nested let + ternary
translate([40, 82, 0])
    let(w = 10, h = 6)
        let(half_w = w / 2)
            cube([w, half_w > 3 ? half_w : 3, h]);
