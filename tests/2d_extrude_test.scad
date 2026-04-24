// Tier 4 visual test — 2-D primitives and extrusions
// Open in ChiselCAD (and optionally OpenSCAD) to compare results.

// Scene 1: linear_extrude a square
translate([0, 0, 0])
    linear_extrude(height=10)
        square([8, 5]);

// Scene 2: linear_extrude a circle  → cylinder-like solid
translate([20, 0, 0])
    linear_extrude(height=15)
        circle(r=4, $fn=32);

// Scene 3: linear_extrude a triangle polygon
translate([40, 0, 0])
    linear_extrude(height=8)
        polygon(points=[[0,0],[10,0],[5,9]]);

// Scene 4: centered extrusion
translate([60, 0, 0])
    linear_extrude(height=10, center=true)
        square([6, 6], center=true);

// Scene 5: rotate_extrude of a circle offset from Z axis → torus
translate([0, 30, 0])
    rotate_extrude($fn=48)
        translate([8, 0, 0])
            circle(r=2, $fn=24);

// Scene 6: rotate_extrude partial sweep → C-shape
translate([25, 30, 0])
    rotate_extrude(angle=270, $fn=48)
        translate([6, 0, 0])
            square([2, 4]);

// Scene 7: linear_extrude with twist → helical prism
translate([50, 30, 0])
    linear_extrude(height=20, twist=90, $fn=24)
        square([5, 5], center=true);

// Scene 8: 2-D boolean inside extrude → hollow tube
translate([0, 65, 0])
    linear_extrude(height=12)
        difference() {
            circle(r=7, $fn=32);
            circle(r=5, $fn=32);
        }

// Scene 9: polygon with paths (ring via two contours)
translate([25, 65, 0])
    linear_extrude(height=6)
        polygon(
            points=[[0,0],[8,0],[8,8],[0,8],[2,2],[6,2],[6,6],[2,6]],
            paths=[[0,1,2,3],[4,5,6,7]]
        );

// Scene 10: scale taper — frustum-like solid
translate([50, 65, 0])
    linear_extrude(height=12, scale=0.3)
        circle(r=6, $fn=32);
