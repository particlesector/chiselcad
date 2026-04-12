// ChiselCAD test file — classic CSG operations
$fn = 48;

difference() {
    cube([30, 30, 30], center=true);
    sphere(r=20);
}
