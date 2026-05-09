// Tier B visual test — string literals, rands, lookup
// Open in ChiselCAD (and optionally OpenSCAD) to compare results.

// --- Scene 1: lookup — height profile tower
// lookup maps x position → cylinder height via linear interpolation
table = [[0, 2], [5, 8], [10, 4], [15, 12], [20, 6]];
for (x = [0, 5, 10, 15, 20])
    translate([x, 0, 0])
        cylinder(h = lookup(x, table), r = 1.8, $fn = 16);

// --- Scene 2: rands — random sphere cluster with fixed seed
// 8 spheres placed at random offsets, all within a known range
offsets = rands(-8, 8, 8, 1337);
for (i = [0:7])
    translate([offsets[i], 20, 0])
        sphere(r = 1.2, $fn = 12);

// --- Scene 3: string literal length drives geometry
// len("chisel") == 6 → 6 columns
word = "chisel";
for (i = [0 : len(word) - 1])
    translate([i * 4, 40, 0])
        cylinder(h = ord(word[i]) - 90, r = 1.5, $fn = 12);

// --- Scene 4: str() used to build a label height
// str(42) has 2 chars → height 2
label_h = len(str(42));
translate([0, 60, 0])
    cube([10, 5, label_h]);

// --- Scene 5: inverse trig drives an angle layout (Tier B math)
// asin(0.5) == 30 degrees, acos(0.5) == 60 degrees
// Build two cylinders rotated by these angles
translate([20, 60, 0])
    rotate([0, asin(0.5), 0])
        cylinder(h = 8, r = 1, $fn = 8);

translate([30, 60, 0])
    rotate([0, acos(0.5), 0])
        cylinder(h = 8, r = 1, $fn = 8);

// --- Scene 6: norm + cross (Tier B vector math)
// norm([3,4,0]) == 5; use as sphere radius
r_val = norm([3, 4, 0]);
translate([0, 80, 0])
    sphere(r = r_val, $fn = 32);

// cross([1,0,0],[0,1,0]) == [0,0,1]; translate along the cross product
v = cross([1, 0, 0], [0, 1, 0]);
translate([v[0] * 10 + 16, v[1] * 10 + 80, v[2] * 10])
    sphere(r = 2, $fn = 16);
