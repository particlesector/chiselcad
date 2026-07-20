// stl_diff — compares two ASCII STL meshes for volumetric equivalence,
// tolerant of completely different tessellation (triangle count, vertex
// order, exact coordinates) between the two files, since it re-tessellates
// nothing: it loads each as a Manifold and measures the volume of their
// symmetric difference, which is ~0 iff they represent the same solid
// regardless of how each one discretized it.
//
// Not part of the CMake build — see tests/tools/README.md.
//
// Usage: stl_diff <a.stl> <b.stl>
#include <manifold/manifold.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace manifold;

// Every "vertex x y z" line in an ASCII STL is one triangle-soup vertex;
// three in a row form one triangle. Ignores every other line (solid/
// endsolid, facet normal, outer loop/endloop, endfacet) — position is all
// that's needed to rebuild a Manifold via MeshGL::Merge().
static MeshGL loadAsciiStl(const std::string& path) {
    std::ifstream in(path);
    MeshGL mesh;
    mesh.numProp = 3;
    std::string line;
    uint32_t vertIdx = 0;
    while (std::getline(in, line)) {
        auto pos = line.find("vertex");
        if (pos == std::string::npos) continue;
        std::istringstream iss(line.substr(pos + 6));
        double x, y, z;
        iss >> x >> y >> z;
        mesh.vertProperties.push_back(static_cast<float>(x));
        mesh.vertProperties.push_back(static_cast<float>(y));
        mesh.vertProperties.push_back(static_cast<float>(z));
        mesh.triVerts.push_back(vertIdx++);
    }
    return mesh;
}

static Manifold loadAsManifold(const std::string& path) {
    MeshGL mesh = loadAsciiStl(path);
    mesh.Merge();
    return Manifold(mesh);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <a.stl> <b.stl>\n", argv[0]);
        return 2;
    }
    Manifold a = loadAsManifold(argv[1]);
    Manifold b = loadAsManifold(argv[2]);

    if (a.Status() != Manifold::Error::NoError || a.IsEmpty()) {
        std::fprintf(stderr, "%s did not load as a valid manifold (status=%d, empty=%d)\n",
                     argv[1], static_cast<int>(a.Status()), a.IsEmpty());
        return 1;
    }
    if (b.Status() != Manifold::Error::NoError || b.IsEmpty()) {
        std::fprintf(stderr, "%s did not load as a valid manifold (status=%d, empty=%d)\n",
                     argv[2], static_cast<int>(b.Status()), b.IsEmpty());
        return 1;
    }

    double volA = a.Volume(), volB = b.Volume();
    Manifold symDiff = (a - b) + (b - a);
    double diffVol = symDiff.Volume();
    double maxVol = volA > volB ? volA : volB;
    double relError = maxVol > 0 ? diffVol / maxVol : (diffVol > 0 ? 1.0 : 0.0);

    std::printf("volume_a=%g volume_b=%g sym_diff_volume=%g rel_error=%g\n",
                volA, volB, diffVol, relError);
    return 0;
}
