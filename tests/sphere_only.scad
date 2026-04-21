// Isolated sphere — no booleans.
// Expected facets for $fn=48:
//   nRings = (48+1)/2 = 24
//   caps: 2 * (48-2) = 92
//   strips: 23 * 48 * 2 = 2208
//   total: 2300 facets, 24*48 = 1152 vertices
$fn = 48;
sphere(r = 5);
