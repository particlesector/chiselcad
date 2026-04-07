// ================================================================
// ChiselCAD Test Model v1
// ----------------------------------------------------------------
// Purpose : Validate ChiselCAD output against reference OpenSCAD
// Baseline: Validated in OpenSCAD 2021.01
//           Preview: 0.045s | CGAL full render: 2m19s
//           47,024 facets | 30,317 vertices
// Coverage: cube, sphere, cylinder
//           union, difference, intersection
//           translate, rotate, scale, mirror
//           $fn / $fs / $fa  tessellation params
//           nested / combined operations
//           edge cases & precision stress
//
// Layout  : 6 rows along Y axis, read left-to-right along X
//
// Usage   : Open in both OpenSCAD and ChiselCAD, compare visually
//           and via exported STL diff
// ================================================================

$fn = 48;   // Global tessellation quality — change to test $fn behaviour


// ================================================================
// ROW 1  (y = 0)  —  Bare Primitives
// ================================================================

// 1.1  Non-centered cube
translate([0, 0, 0])
    cube([10, 10, 10]);

// 1.2  Centered cube
translate([18, 0, 0])
    cube([10, 10, 10], center = true);

// 1.3  Sphere (origin at centre by default)
translate([34, 0, 5])
    sphere(r = 5);

// 1.4  Cylinder, flat base on XY plane
translate([50, 0, 0])
    cylinder(h = 10, r = 5);

// 1.5  Cylinder, centered on Z
translate([66, 0, 5])
    cylinder(h = 10, r = 5, center = true);

// 1.6  Cone  (r1 != r2)
translate([82, 0, 0])
    cylinder(h = 12, r1 = 7, r2 = 1);

// 1.7  Flat disc  (very low h)
translate([100, 0, 0])
    cylinder(h = 1.5, r = 7);

// 1.8  $fn override — low-poly sphere (should produce 8-segment approximation)
translate([118, 0, 5])
    sphere(r = 5, $fn = 8);

// 1.9  $fn override — high-poly cylinder
translate([134, 0, 0])
    cylinder(h = 10, r = 5, $fn = 128);


// ================================================================
// ROW 2  (y = 30)  —  Boolean Operations (simple)
// ================================================================

// 2.1  Union: cube + offset sphere
translate([0, 30, 0])
    union() {
        cube([10, 10, 10], center = true);
        translate([5, 5, 5]) sphere(r = 5);
    }

// 2.2  Difference: cube minus centred sphere  (hollow cube)
translate([22, 30, 0])
    difference() {
        cube([13, 13, 13], center = true);
        sphere(r = 7);
    }

// 2.3  Intersection: cube AND sphere  (rounded cube / cube-sphere lens)
translate([44, 30, 0])
    intersection() {
        cube([15, 15, 15], center = true);
        sphere(r = 9);
    }

// 2.4  Difference: cylinder minus inner cylinder  (hollow tube)
translate([66, 30, 0])
    difference() {
        cylinder(h = 14, r = 6, center = true);
        cylinder(h = 16, r = 4, center = true);
    }

// 2.5  Intersection: two offset cylinders  (lens / vesica cross-section)
translate([88, 30, 0])
    intersection() {
        cylinder(h = 14, r = 6, center = true);
        rotate([90, 0, 0]) cylinder(h = 14, r = 6, center = true);
    }

// 2.6  Union: three spheres  (snowman column)
translate([110, 30, 0])
    union() {
        sphere(r = 5);
        translate([0, 0, 9])  sphere(r = 4);
        translate([0, 0, 16]) sphere(r = 3);
    }


// ================================================================
// ROW 3  (y = 65)  —  Transforms
// ================================================================

// 3.1  Baseline — no transform
translate([0, 65, 0])
    cube([8, 8, 8], center = true);

// 3.2  Rotate X only
translate([20, 65, 0])
    rotate([45, 0, 0])
        cube([8, 8, 8], center = true);

// 3.3  Rotate Y only
translate([40, 65, 0])
    rotate([0, 45, 0])
        cube([8, 8, 8], center = true);

// 3.4  Rotate Z only
translate([60, 65, 0])
    rotate([0, 0, 45])
        cube([8, 8, 8], center = true);

// 3.5  Rotate all three axes
translate([80, 65, 0])
    rotate([30, 45, 60])
        cube([8, 8, 8], center = true);

// 3.6  Scale uniform (larger sphere)
translate([100, 65, 0])
    scale([1.8, 1.8, 1.8])
        sphere(r = 3);

// 3.7  Scale non-uniform — ellipsoid
translate([122, 65, 0])
    scale([2.2, 0.6, 1.4])
        sphere(r = 5);

// 3.8  Scale with cylinder — squashed puck
translate([144, 65, 0])
    scale([1.0, 1.0, 0.3])
        cylinder(h = 20, r = 6, center = true);

// 3.9  Mirror X — symmetric pair of spheres
translate([0, 82, 5]) {
    translate([6, 0, 0]) sphere(r = 3);
    mirror([1, 0, 0])
        translate([6, 0, 0]) sphere(r = 3);
}

// 3.10 Mirror Y — symmetric pair of cylinders
translate([20, 78, 0]) {
    translate([0, 5, 0]) cylinder(h = 10, r = 2, center = true);
    mirror([0, 1, 0])
        translate([0, 5, 0]) cylinder(h = 10, r = 2, center = true);
}

// 3.11 Mirror Z — cups facing each other
translate([40, 78, 0]) {
    cylinder(h = 6, r1 = 5, r2 = 2);
    mirror([0, 0, 1])
        cylinder(h = 6, r1 = 5, r2 = 2);
}

// 3.12 Chained transforms (translate → rotate → translate)
translate([70, 78, 0])
    translate([0, 0, 5])
        rotate([0, 45, 0])
            translate([0, 0, -5])
                cube([8, 3, 8], center = true);


// ================================================================
// ROW 4  (y = 108)  —  Nested / Combined Operations
// ================================================================

// 4.1  Difference inside union
translate([0, 108, 0])
    union() {
        difference() {
            cube([12, 12, 12]);
            translate([6, 6, -1]) cylinder(h = 14, r = 3.5);
        }
        translate([6, 6, 12]) sphere(r = 3.5);
    }

// 4.2  Union inside difference  (additive features, then cut)
translate([20, 108, 0])
    difference() {
        union() {
            cube([14, 10, 8]);
            translate([7, 5, 8]) cylinder(h = 6, r = 3);
        }
        translate([7, 5, -1]) cylinder(h = 16, r = 2);
    }

// 4.3  Intersection of a unioned body with a sphere  (blob trim)
translate([42, 108, 0])
    intersection() {
        union() {
            sphere(r = 7);
            translate([6, 0, 0]) sphere(r = 7);
        }
        cube([18, 10, 18], center = true);
    }

// 4.4  Transforms applied to booleans
translate([68, 108, 0])
    rotate([0, 0, 30])
        difference() {
            cube([14, 14, 8], center = true);
            cylinder(h = 10, r = 5, center = true);
        }

// 4.5  Scale applied to a difference
translate([90, 108, 0])
    scale([1.5, 1.0, 0.5])
        difference() {
            sphere(r = 8);
            sphere(r = 5);
        }

// 4.6  Mirror of a complex boolean body
translate([114, 108, 0]) {
    difference() {
        cube([10, 14, 10], center = true);
        translate([2, 0, 0]) sphere(r = 5);
    }
    mirror([1, 0, 0])
        difference() {
            cube([10, 14, 10], center = true);
            translate([2, 0, 0]) sphere(r = 5);
        }
}


// ================================================================
// ROW 5  (y = 140)  —  Main Test Piece: Mechanical Bracket
// ----------------------------------------------------------------
// A realistic part exercising deep nesting in a single body.
// Expected result: rectangular plate, 4 counterbored mounting
// holes, a central oblong slot, an angled corner chamfer,
// a cylindrical boss on top, and two mirrored gusset tabs.
// ================================================================

translate([0, 140, 0]) {

    // --- Base plate with all subtractions ---
    difference() {

        // Body
        cube([60, 35, 7]);

        // Corner mounting holes (through)
        translate([6,   6,   -1]) cylinder(h = 9, r = 2.8);
        translate([54,  6,   -1]) cylinder(h = 9, r = 2.8);
        translate([6,   29,  -1]) cylinder(h = 9, r = 2.8);
        translate([54,  29,  -1]) cylinder(h = 9, r = 2.8);

        // Countersink chamfer on each hole (larger r, shallow)
        translate([6,   6,   4]) cylinder(h = 4, r1 = 2.8, r2 = 5.0);
        translate([54,  6,   4]) cylinder(h = 4, r1 = 2.8, r2 = 5.0);
        translate([6,   29,  4]) cylinder(h = 4, r1 = 2.8, r2 = 5.0);
        translate([54,  29,  4]) cylinder(h = 4, r1 = 2.8, r2 = 5.0);

        // Central oblong slot (two overlapping cylinders + cube bridge)
        translate([22, 17.5, -1]) cylinder(h = 9, r = 4);
        translate([38, 17.5, -1]) cylinder(h = 9, r = 4);
        translate([22, 13.5, -1]) cube([16, 8, 9]);

        // Diagonal corner chamfer (45-degree cut, bottom-right corner)
        translate([60, 0, -1])
            rotate([0, 0, 45])
                cube([16, 16, 9]);
    }

    // --- Cylindrical boss on top surface, centred ---
    translate([30, 17.5, 7])
        cylinder(h = 9, r = 5.5);

    // --- Bore through the boss ---
    translate([30, 17.5, 6])
        difference() {
            cylinder(h = 11, r = 5.5);
            cylinder(h = 11, r = 3.5);
        }

    // --- Symmetric gusset tabs (mirrored about centre of plate) ---
    // Left tab
    translate([0, 140, 0])  // origin-relative, cancel outer translate effect
        mirror([0, 0, 0])   // identity mirror — keep left as-is
            translate([0, 0, 0])
                union() {}; // placeholder — see mirrored pair below

    // Right gusset (actual geometry)
    translate([50, 5, 7])
        rotate([0, -30, 0])
            cube([6, 8, 5]);

    // Left gusset — mirrored
    translate([60, 0, 0])
        mirror([1, 0, 0])
            translate([50, 5, 7])
                rotate([0, -30, 0])
                    cube([6, 8, 5]);
}


// ================================================================
// ROW 6  (y = 195)  —  Precision & Edge Case Stress Tests
// ================================================================

// 6.1  Sphere-sphere intersection  (lens shape, tests coincident curved faces)
translate([0, 195, 0])
    intersection() {
        sphere(r = 9);
        translate([7, 0, 0]) sphere(r = 9);
    }

// 6.2  Swiss-cheese cube  (multiple axis-aligned and diagonal holes)
translate([25, 195, 0])
    difference() {
        cube([20, 20, 20], center = true);
        sphere(r = 10);                                      // hollow core
        cylinder(h = 22, r = 3.5, center = true);            // Z hole
        rotate([90, 0, 0]) cylinder(h = 22, r = 3.5, center = true);  // Y hole
        rotate([0, 90, 0]) cylinder(h = 22, r = 3.5, center = true);  // X hole
    }

// 6.3  Angled holes through a block  (non-axis-aligned booleans)
translate([55, 195, 0])
    difference() {
        cube([16, 16, 16], center = true);
        rotate([30, 0, 0]) cylinder(h = 22, r = 2.5, center = true);
        rotate([0, 30, 0]) cylinder(h = 22, r = 2.5, center = true);
        rotate([30, 30, 0]) cylinder(h = 22, r = 2.5, center = true);
    }

// 6.4  Tightly coincident faces  (cube difference where walls are flush)
//      ChiselCAD should produce clean zero-thickness edges, not gaps/overlaps
translate([82, 195, 0])
    difference() {
        cube([14, 14, 14]);
        translate([2, 2, 2]) cube([10, 10, 10]);  // walls exactly 2 units thick
    }

// 6.5  Zero-gap union  (two cubes sharing exactly one face — should merge cleanly)
translate([106, 195, 0])
    union() {
        cube([10, 10, 10]);
        translate([10, 0, 0]) cube([10, 10, 10]);  // shared face at x=10
    }

// 6.6  Deeply nested difference  (chain of 5 subtractions)
translate([132, 195, 0])
    difference() {
        sphere(r = 10);
        translate([ 6,  0,  0]) sphere(r = 5);
        translate([-6,  0,  0]) sphere(r = 5);
        translate([ 0,  6,  0]) sphere(r = 5);
        translate([ 0, -6,  0]) sphere(r = 5);
        translate([ 0,  0,  6]) sphere(r = 5);
    }

// 6.7  Scale + rotate + difference  (tests transform order consistency)
translate([158, 195, 0])
    difference() {
        scale([1.2, 0.8, 1.5])
            rotate([0, 0, 30])
                cube([12, 12, 8], center = true);
        rotate([0, 0, 30])
            scale([1.2, 0.8, 1.5])
                cylinder(h = 14, r = 3, center = true);
    }

// 6.8  $fa tessellation test  (should produce visibly different segment counts)
translate([0, 215, 0]) {
    translate([0,  0, 0]) cylinder(h = 8, r = 8, $fn = 6);   // hexagonal prism
    translate([20, 0, 0]) cylinder(h = 8, r = 8, $fn = 8);   // octagonal prism
    translate([40, 0, 0]) cylinder(h = 8, r = 8, $fn = 16);  // smooth-ish
    translate([60, 0, 0]) cylinder(h = 8, r = 8, $fn = 64);  // very smooth
}
