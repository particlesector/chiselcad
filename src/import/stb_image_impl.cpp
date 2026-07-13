// Single translation unit for stb_image's implementation (third_party/stb/
// stb_image.h is included everywhere else in declaration-only mode).
// Public domain vendored library — warnings disabled for this TU only via
// CMakeLists.txt (set_source_files_properties), not touched/reformatted to
// keep future upstream updates a clean diff.
#define STB_IMAGE_IMPLEMENTATION
// Only PNG decoding is needed (surface()'s heightmap support); disabling the
// rest keeps this TU's compile time and binary size down.
#define STBI_ONLY_PNG
#include <stb/stb_image.h>
