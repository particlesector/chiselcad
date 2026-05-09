// Tier C visual test — children(), $children, echo(), assert(), recursive functions
// Open in ChiselCAD (and optionally OpenSCAD) to compare results.

// --- Scene 1: children() passthrough — module wraps geometry
// wrap() adds a base plate under whatever children are passed
module wrap_on_base() {
    translate([0, 0, 2]) children();
    cube([12, 12, 2]);  // base plate
}

wrap_on_base()
    sphere(r = 4, $fn = 32);

// --- Scene 2: children() repeated — tile a single child
module tile(n, spacing) {
    for (i = [0 : n - 1])
        translate([i * spacing, 0, 0])
            children();
}

translate([20, 0, 0])
    tile(5, 5)
        cylinder(h = 6, r = 1.5, $fn = 16);

// --- Scene 3: children(i) indexed access — select specific child
module first_and_last(total) {
    translate([-4, 0, 0]) children(0);
    translate([ 4, 0, 0]) children(total - 1);
}

translate([0, 22, 0])
    first_and_last(3) {
        sphere(r = 2, $fn = 24);   // child 0
        cube([3, 3, 3]);            // child 1 (skipped)
        cylinder(h = 4, r = 2, $fn = 16); // child 2
    }

// --- Scene 4: $children drives branching
module adaptive() {
    if ($children == 1) {
        // single child: center it on a pedestal
        translate([0, 0, 3]) children(0);
        cylinder(h = 3, r = 5, $fn = 32);
    } else {
        // multiple children: spread them out
        for (i = [0 : $children - 1])
            translate([i * 8 - ($children - 1) * 4, 0, 0])
                children(i);
    }
}

translate([0, 44, 0]) {
    // single child → pedestal
    adaptive()
        sphere(r = 3, $fn = 32);
}

translate([30, 44, 0]) {
    // two children → spread
    adaptive() {
        cube([5, 5, 5]);
        sphere(r = 3, $fn = 20);
    }
}

// --- Scene 5: echo() — messages visible in diagnostics panel
echo("Tier C test loaded");
r_val = 5;
echo("r_val =", r_val);
echo("pi approx =", 3.14159);
echo("vector:", [1, 2, 3]);

// Geometry to confirm the file ran
translate([50, 0, 0])
    cube([3, 3, 3]);

// --- Scene 6: assert() — passes silently, halts on failure
function clamp(v, lo, hi) = v < lo ? lo : (v > hi ? hi : v);

module safe_sphere(r) {
    assert(r > 0, str("radius must be positive, got ", r));
    sphere(r = r, $fn = 24);
}

translate([60, 0, 0]) {
    safe_sphere(4);              // passes: r > 0
    translate([12, 0, 0])
        safe_sphere(clamp(10, 1, 6)); // passes: clamped to 6
}

// --- Scene 7: recursive function drives a Fibonacci tower
function fib(n) = n <= 1 ? n : fib(n - 1) + fib(n - 2);

translate([0, 65, 0])
    for (i = [1 : 7])
        translate([(i - 1) * 7, 0, 0])
            cylinder(h = fib(i), r = 2, $fn = 12);

// --- Scene 8: children() inside a recursive-like repeating structure
module stack(n, h) {
    if (n > 0) {
        translate([0, 0, 0]) children();
        translate([0, 0, h])
            stack(n - 1, h)
                children();
    }
}

translate([60, 65, 0])
    stack(4, 5)
        cube([8, 8, 4]);
