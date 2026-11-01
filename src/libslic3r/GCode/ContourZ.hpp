#ifndef slic3r_GCode_ContourZ_hpp_
#define slic3r_GCode_ContourZ_hpp_

// Z contouring (Z anti-aliasing, "ZAA") of top surfaces: points of top-surface infill, ironing and walls
// are moved up/down within the layer to follow the true model surface, reducing stair-stepping on
// gently sloped tops.
//
// Port of OrcaSlicer's ContourZ (by Aleksandr Dobkin, adob/BambuStudio-ZAA, based on Song, Ray, Sokolov,
// Lefebvre 2017 "Anti-aliasing for fused filament deposition"). Unlike Orca, which stores per-point Z in
// 3D extrusion paths at slice time, this port computes the Z offsets at G-code export time, so the 2D
// extrusion path data structures stay unchanged.

#include "../ExPolygon.hpp"
#include "../ExtrusionEntity.hpp"
#include "../SLA/IndexedMesh.hpp"
#include "../TriangleMesh.hpp"

#include <memory>
#include <vector>

namespace Slic3r {

class Layer;
class PrintObject;

struct ZaaPoint
{
    Point  p;   // object coordinates (scaled)
    double dz;  // mm, relative to the nominal top of the layer
};
using ZaaPoints = std::vector<ZaaPoint>;

struct ZaaParams
{
    double min_z                    { 0.05 }; // zaa_min_z
    double minimize_perimeter_angle { 35. };  // zaa_minimize_perimeter_height, degrees, 0 = off
};

class ZaaMesh
{
public:
    explicit ZaaMesh(const PrintObject &object);

    // Height of the model surface above the nominal top of the layer at pt (mm), hit normal in *normal.
    // Returns false if there is no surface above the slicing plane at pt.
    bool surface_dz(const Layer &layer, const Point &pt, double &dz, Vec3d *normal = nullptr) const;

    // Average surface height (mm, relative to the layer top) sampled along a polyline; used for ordering.
    double mean_dz(const Layer &layer, const Polyline &pl) const;

private:
    // IndexedMesh only keeps a pointer to the triangles: m_triangles must be declared (constructed) first.
    TriangleMesh     m_triangles;
    sla::IndexedMesh m_mesh;
    double           m_z_min_object; // object_print_z_min: offset between print_z and object (slicing) z
};

// Z contoured points for the path, densified to ~0.1 mm. Empty if the path is not eligible or nothing needed contouring.
ZaaPoints zaa_contour_path(const ZaaMesh &mesh, const Layer &layer, const ExtrusionPath &path, const ZaaParams &params);

bool zaa_role_eligible(ExtrusionRole role);

// XY shape of a polyline with arc fitting segments expanded back into points.
Points zaa_expand_points(const Polyline &pl);

// Split a zig-zag top surface path into its individual strokes (at turns sharper than 90 degrees),
// keeping short connectors attached to the stroke before them. Used to order strokes by height.
Polylines zaa_split_strokes(const Polyline &pl, double min_stroke_mm = 1.0);

} // namespace Slic3r

#endif // slic3r_GCode_ContourZ_hpp_
