#include "ContourZ.hpp"

#include "../Circle.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"

#include <cmath>

namespace Slic3r {

static constexpr double ZAA_RESOLUTION_MM = 0.1;

bool zaa_role_eligible(ExtrusionRole role)
{
    return role == erTopSolidInfill || role == erIroning || role == erPerimeter || role == erExternalPerimeter;
}

ZaaMesh::ZaaMesh(const PrintObject &object)
    // Same transformation as used for slicing (PrintObject::slice_volumes), so mesh z == layer slice_z frame.
    : m_triangles([&object]() {
          TriangleMesh mesh = object.model_object()->raw_mesh();
          mesh.transform(object.trafo_centered());
          return mesh;
      }())
    , m_mesh(m_triangles)
    , m_z_min_object(object.slicing_parameters().object_print_z_min)
{}

bool ZaaMesh::surface_dz(const Layer &layer, const Point &pt, double &dz, Vec3d *normal) const
{
    const sla::IndexedMesh::hit_result hit = m_mesh.query_ray_hit({ unscale<double>(pt.x()), unscale<double>(pt.y()), layer.slice_z }, { 0., 0., 1. });
    if (!hit.is_hit())
        return false;
    const double layer_top = layer.print_z - m_z_min_object;
    dz = layer.slice_z + hit.distance() - layer_top;
    if (normal != nullptr)
        *normal = hit.normal();
    return true;
}

double ZaaMesh::mean_dz(const Layer &layer, const Polyline &pl) const
{
    const Points pts = pl.points.size() > 2 ? pl.equally_spaced_points(std::max(scale_(1.), pl.length() / 16.)) : pl.points;
    double sum = 0.;
    int    n   = 0;
    for (const Point &p : pts) {
        double dz;
        if (surface_dz(layer, p, dz)) {
            sum += dz;
            ++n;
        }
    }
    return n == 0 ? 0. : sum / n;
}

// XY shape of the path, with arcs from arc fitting expanded back into points.
Points zaa_expand_points(const Polyline &pl)
{
    if (pl.fitting_result.empty())
        return pl.points;
    Points out;
    for (const PathFittingData &seg : pl.fitting_result) {
        if (seg.path_type == EMovePathType::Linear_move) {
            for (size_t i = seg.start_point_index; i <= seg.end_point_index && i < pl.points.size(); ++i)
                if (out.empty() || out.back() != pl.points[i])
                    out.push_back(pl.points[i]);
        } else {
            const ArcSegment &arc = seg.arc_data;
            const double a0    = std::atan2(double(arc.start_point.y() - arc.center.y()), double(arc.start_point.x() - arc.center.x()));
            // angle_radians is signed: negative for clockwise arcs (ArcSegment::update_angle_and_length).
            const double sweep = arc.angle_radians;
            const int    n     = std::max(2, int(std::ceil(std::abs(sweep) * arc.radius / scale_(ZAA_RESOLUTION_MM))));
            if (out.empty() || out.back() != arc.start_point)
                out.push_back(arc.start_point);
            for (int i = 1; i < n; ++i) {
                const double a = a0 + sweep * i / n;
                out.emplace_back(arc.center.x() + coord_t(std::lround(arc.radius * std::cos(a))), arc.center.y() + coord_t(std::lround(arc.radius * std::sin(a))));
            }
            out.push_back(arc.end_point);
        }
    }
    return out;
}

Polylines zaa_split_strokes(const Polyline &pl, double min_stroke_mm)
{
    const Points pts = zaa_expand_points(pl);
    Polylines    out;
    if (pts.size() < 2)
        return out;
    Polyline cur;
    cur.points.push_back(pts.front());
    for (size_t i = 1; i < pts.size(); ++i) {
        cur.points.push_back(pts[i]);
        if (i + 1 < pts.size()) {
            const Vec2d v1 = (pts[i] - pts[i - 1]).cast<double>();
            const Vec2d v2 = (pts[i + 1] - pts[i]).cast<double>();
            if (v1.dot(v2) < 0.) { // turn sharper than 90 degrees
                out.push_back(cur);
                cur.points = { pts[i] };
            }
        }
    }
    out.push_back(cur);
    // Attach short pieces (zig-zag connectors) to the previous stroke.
    Polylines merged;
    for (Polyline &p : out) {
        if (!merged.empty() && unscale<double>(p.length()) < min_stroke_mm)
            merged.back().points.insert(merged.back().points.end(), p.points.begin() + 1, p.points.end());
        else
            merged.push_back(std::move(p));
    }
    return merged;
}

ZaaPoints zaa_contour_path(const ZaaMesh &mesh, const Layer &layer, const ExtrusionPath &path, const ZaaParams &params)
{
    ZaaPoints out;
    if (!zaa_role_eligible(path.role()) || path.is_force_no_extrusion())
        return out;
    const Points pts = zaa_expand_points(path.polyline);
    if (pts.size() < 2)
        return out;

    const double height     = layer.height;
    const double half_width = path.width / 2.;
    double       max_up     = params.min_z;
    double       min_down   = -(height - params.min_z);
    if (path.role() == erIroning) {
        max_up   = height;
        min_down = -(height + 0.1);
    }
    const bool perimeter = is_perimeter(path.role());

    auto contour_dz = [&](const Point &p) {
        double d;
        Vec3d  normal;
        if (!mesh.surface_dz(layer, p, d, &normal))
            return 0.;
        if (perimeter && d > min_down && params.minimize_perimeter_angle > 0.) {
            // Lower walls on steep slopes to the height of the model edge.
            const double slope = std::acos(std::min(1., std::abs(normal.normalized().z())));
            if (slope * 180. / M_PI > params.minimize_perimeter_angle)
                d = std::max(min_down, d - half_width * std::sin(slope));
        }
        // Too far from the surface: not a top surface (e.g. a wall under an overhang), do not contour.
        if (d < -height || d > max_up + 0.03)
            return 0.;
        d = std::clamp(d, min_down, max_up);
        // Do not raise walls, that would show as a seam.
        if (perimeter && d > 0.)
            d = 0.;
        return d;
    };

    bool contoured = false;
    auto push = [&](const Point &p, double dz, bool original) {
        // Ignore sub-5 micron offsets (G-code Z resolution is 1 micron): not worth a contoured path.
        if (std::abs(dz) > 0.005)
            contoured = true;
        // Merge collinear (in 3D) points, except original path points.
        if (!original && out.size() >= 2) {
            const ZaaPoint &a = out[out.size() - 2], &b = out.back();
            const Vec3d     va(unscale<double>(a.p.x()), unscale<double>(a.p.y()), a.dz);
            const Vec3d     vb(unscale<double>(b.p.x()), unscale<double>(b.p.y()), b.dz);
            const Vec3d     vp(unscale<double>(p.x()), unscale<double>(p.y()), dz);
            const Vec3d     dir = vb - va;
            if (dir.squaredNorm() > 0. && (vp - va).cross(dir).squaredNorm() / dir.squaredNorm() < 1e-8) {
                out.back() = { p, dz };
                return;
            }
        }
        out.push_back({ p, dz });
    };

    push(pts.front(), contour_dz(pts.front()), true);
    for (size_t i = 1; i < pts.size(); ++i) {
        const Point &a   = pts[i - 1];
        const Point &b   = pts[i];
        const double len = unscale<double>((b - a).cast<double>().norm());
        const int    n   = std::max(1, int(std::ceil(len / ZAA_RESOLUTION_MM)));
        for (int k = 1; k <= n; ++k) {
            const Point p = k == n ? b : Point(a.x() + coord_t(std::lround(double(b.x() - a.x()) * k / n)), a.y() + coord_t(std::lround(double(b.y() - a.y()) * k / n)));
            push(p, contour_dz(p), k == n);
        }
    }
    if (!contoured)
        out.clear();
    return out;
}

} // namespace Slic3r
