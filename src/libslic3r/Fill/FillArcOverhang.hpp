#ifndef slic3r_FillArcOverhang_hpp_
#define slic3r_FillArcOverhang_hpp_

// Arc overhangs: fill an unsupported region with concentric arcs that grow out of the
// supported edge, so that every extrusion is anchored on an already printed one.
//
// C++ port of the breadth-first arc generator from
//   https://github.com/nicolai-wachenschwan/arc-overhang-prusaslicer-integration
// which is based on the original idea and code by Steven McCulloch
//   https://github.com/stmcculloch/arc-overhang  (also mirrored at CNCKitchen/arc-overhang)
// Both are GPL-3.0; this port is distributed under BambuStudio's AGPL-3.0.

#include "../ExPolygon.hpp"
#include "../Polygon.hpp"
#include "../Polyline.hpp"

namespace Slic3r {

struct ArcOverhangParams
{
    // All lengths scaled.
    coord_t spacing                 { 0 }; // radial distance between neighbouring arcs
    coord_t r_min_start             { 0 }; // radius of the first arc around the very first center
    coord_t r_min                   { 0 }; // radius of the first arc around subsequent centers
    coord_t r_max                   { 0 }; // maximum arc radius
    coord_t center_offset           { 0 }; // pull new centers back into the already filled area by this much
    coord_t max_dist_from_perimeter { 0 }; // stop branching once the gap to the region boundary is smaller
    coord_t max_segment_length      { 0 }; // circle discretisation
    size_t  min_start_arcs          { 2 };
    size_t  max_centers             { 2000 };
    // Extra arc layers: start as far as possible from this point (the start of the arcs below),
    // so the arcs of this layer cross the ones below instead of stacking on them.
    bool    avoid_start             { false };
    Point   avoid_point;
};

struct ArcOverhangArcs
{
    // Arcs in print order. Arcs around one center are ordered by increasing radius,
    // centers are ordered so that each one starts on an arc printed before.
    Polylines  arcs;
    // Part of the input region not covered by any arc disk.
    ExPolygons remaining;
    // Center of the first arcs.
    Point      start_point;
};

// region : area to fill, already extended a little into the supported area.
// anchor : supported area (previous layer), used to find the starting edge.
// Returns empty arcs if no usable starting edge was found.
ArcOverhangArcs generate_arc_overhang(const ExPolygon &region, const Polygons &anchor, const ArcOverhangParams &params);

} // namespace Slic3r

#endif // slic3r_FillArcOverhang_hpp_
