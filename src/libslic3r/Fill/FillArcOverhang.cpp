#include "FillArcOverhang.hpp"

#include "../ClipperUtils.hpp"
#include "../BoundingBox.hpp"
#include "../Line.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {

namespace {

Points circle_points(const Point &c, double r, coord_t max_segment_length)
{
    const size_t n = std::clamp<size_t>(size_t(std::ceil(2. * M_PI * r / double(std::max<coord_t>(max_segment_length, 1)))), 32, 1440);
    Points pts;
    pts.reserve(n + 1);
    for (size_t i = 0; i < n; ++i) {
        const double a = 2. * M_PI * double(i) / double(n);
        pts.emplace_back(c.x() + coord_t(std::lround(r * std::cos(a))), c.y() + coord_t(std::lround(r * std::sin(a))));
    }
    return pts;
}

Polyline circle_polyline(const Point &c, double r, coord_t max_segment_length)
{
    Polyline pl;
    pl.points = circle_points(c, r, max_segment_length);
    pl.points.push_back(pl.points.front());
    return pl;
}

Polygon disk(const Point &c, double r, coord_t max_segment_length)
{
    return Polygon(circle_points(c, r, max_segment_length));
}

// Clip a closed circle (as polyline) against the space and glue back the piece split at the seam.
Polylines clip_circle(const Polyline &circle, const ExPolygons &space, double min_length)
{
    Polylines pieces = intersection_pl(circle, space);
    if (pieces.size() > 1) {
        const Point &seam = circle.points.front();
        auto touches = [&seam](const Polyline &pl) { return pl.first_point() == seam || pl.last_point() == seam; };
        auto it_a = std::find_if(pieces.begin(), pieces.end(), touches);
        auto it_b = it_a == pieces.end() ? pieces.end() : std::find_if(it_a + 1, pieces.end(), touches);
        if (it_b != pieces.end()) {
            Polyline &a = *it_a;
            Polyline  b = std::move(*it_b);
            if (a.last_point() != seam) a.reverse();
            if (b.first_point() != seam) b.reverse();
            a.points.insert(a.points.end(), b.points.begin() + 1, b.points.end());
            pieces.erase(it_b);
        }
    }
    pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [min_length](const Polyline &pl) { return pl.length() < min_length; }), pieces.end());
    return pieces;
}

double distance_to_lines(const Point &p, const Lines &lines)
{
    double best = std::numeric_limits<double>::max();
    for (const Line &l : lines)
        best = std::min(best, l.distance_to_squared(p));
    return std::sqrt(best);
}

double max_distance_to_bbox(const Point &c, const BoundingBox &bb)
{
    double best = 0.;
    for (const Point &corner : { bb.min, bb.max, Point(bb.min.x(), bb.max.y()), Point(bb.max.x(), bb.min.y()) })
        best = std::max(best, (corner - c).cast<double>().norm());
    return best;
}

// Angular extent of an arc piece around its center: start angle in [0, 2pi) and positive sweep.
struct AngularSpan { double start; double sweep; };

AngularSpan angular_span(const Polyline &pl, const Point &c)
{
    auto angle = [&c](const Point &p) { return std::atan2(double(p.y() - c.y()), double(p.x() - c.x())); };
    const double a0    = angle(pl.points.front());
    double       prev  = a0;
    double       sweep = 0.;
    for (size_t i = 1; i < pl.points.size(); ++i) {
        const double a = angle(pl.points[i]);
        double       d = a - prev;
        if (d > M_PI) d -= 2. * M_PI;
        else if (d < -M_PI) d += 2. * M_PI;
        sweep += d;
        prev = a;
    }
    double start = sweep < 0. ? a0 + sweep : a0;
    start = std::fmod(start + 4. * M_PI, 2. * M_PI);
    return { start, std::abs(sweep) };
}

bool angle_in_span(double a, const AngularSpan &s, double tol)
{
    const double d = std::fmod(a - (s.start - tol) + 6. * M_PI, 2. * M_PI);
    return d <= s.sweep + 2. * tol;
}

bool spans_overlap(const AngularSpan &a, const AngularSpan &b, double tol)
{
    return angle_in_span(a.start, b, tol) || angle_in_span(b.start, a, tol);
}

double total_area(const ExPolygons &expolys)
{
    double a = 0.;
    for (const ExPolygon &e : expolys) a += e.area();
    return a;
}

Point move_toward(const Point &from, const Point &to, double dist)
{
    Vec2d v   = (to - from).cast<double>();
    double len = v.norm();
    if (len < 1.)
        return from;
    v *= std::min(dist, len) / len;
    return Point(from.x() + coord_t(std::lround(v.x())), from.y() + coord_t(std::lround(v.y())));
}

// Candidate centers on the starting edge: preferred point first (middle of the longest edge, biased
// towards corners like the reference implementation), then evenly spread fallbacks.
Points start_candidates(const Polylines &start_lines, coord_t spacing)
{
    Points out;
    if (start_lines.empty())
        return out;
    std::vector<size_t> order(start_lines.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return start_lines[a].length() > start_lines[b].length(); });

    for (size_t li : order) {
        const Polyline &ls     = start_lines[li];
        const double    length = ls.length();
        if (ls.points.size() < 2 || length < double(spacing))
            continue;
        // Scored point: vertices (bonus for corners) and segment midpoints, preferring the middle of the
        // edge so the first arcs are half circles anchored at both ends.
        {
            double best_score = -1.;
            Point  best_pt    = ls.points.front();
            double acc        = 0.;
            for (size_t i = 0; i < ls.points.size(); ++i) {
                if (i > 0) {
                    const double seg = (ls.points[i] - ls.points[i - 1]).cast<double>().norm();
                    if (seg > 2. * double(spacing)) {
                        const double score = 1. - std::abs((acc + 0.5 * seg) / length - 0.5);
                        if (score > best_score) { best_score = score; best_pt = (ls.points[i] + ls.points[i - 1]) / 2; }
                    }
                    acc += seg;
                }
                if (i == 0 || i + 1 == ls.points.size())
                    continue;
                const double length_score = 1. - std::abs(acc / length - 0.5);
                Vec2d v1 = (ls.points[i] - ls.points[i - 1]).cast<double>();
                Vec2d v2 = (ls.points[i + 1] - ls.points[i]).cast<double>();
                double angle_score = 0.;
                if (v1.norm() > 0. && v2.norm() > 0.) {
                    const double cosang = std::clamp(v1.normalized().dot(v2.normalized()), -1., 1.);
                    angle_score = 0.2 * std::abs(std::sin(std::acos(cosang)));
                }
                if (length_score + angle_score > best_score) { best_score = length_score + angle_score; best_pt = ls.points[i]; }
            }
            out.emplace_back(best_pt);
        }
        // Fallbacks: points spread along the line, ordered by distance from its middle.
        Points spread = ls.equally_spaced_points(std::max<double>(length / 12., double(spacing)));
        std::sort(spread.begin(), spread.end(), [&](const Point &a, const Point &b) {
            auto rel = [&](const Point &p) {
                double best = std::numeric_limits<double>::max(), at = 0., acc = 0.;
                for (size_t i = 1; i < ls.points.size(); ++i) {
                    const Line l(ls.points[i - 1], ls.points[i]);
                    const double d = l.distance_to_squared(p);
                    if (d < best) { best = d; at = acc + (p - l.a).cast<double>().norm(); }
                    acc += l.length();
                }
                return std::abs(at / length - 0.5);
            };
            return rel(a) < rel(b);
        });
        append(out, std::move(spread));
    }
    return out;
}

} // namespace

ArcOverhangArcs generate_arc_overhang(const ExPolygon &region, const Polygons &anchor, const ArcOverhangParams &p)
{
    ArcOverhangArcs result;
    result.remaining = { region };
    if (p.spacing <= 0 || p.r_max <= p.r_min_start)
        return result;

    const double spacing    = double(p.spacing);
    const double min_length = spacing;
    const Lines  boundary   = to_lines(region);

    // Starting edge: part of the region boundary that rests on the previous layer.
    Polylines start_lines = intersection_pl(to_polylines(region), offset(anchor, float(scale_(0.05))));
    if (start_lines.empty())
        return result;

    ExPolygons &remaining = result.remaining;

    // Grow concentric arcs around c, clipped to the remaining space. A piece is only kept if it overlaps
    // (in angle) a kept piece of the previous ring, i.e. it is printed onto the arc before it; pieces in
    // separate lobes would be printed in mid air. Growth stops at the first ring without kept pieces.
    // Returns the radius of the last ring, or -1 when nothing could be placed.
    auto grow = [&](const Point &c, double r0, std::vector<Polylines> &rings) -> double {
        rings.clear();
        const double r_stop = std::min(double(p.r_max), max_distance_to_bbox(c, get_extents(remaining)));
        std::vector<AngularSpan> prev_spans;
        double r_last      = -1.;
        int    empty_start = 0;
        for (double r = r0; r <= r_stop; r += spacing) {
            Polylines pieces = clip_circle(circle_polyline(c, r, p.max_segment_length), remaining, min_length);
            Polylines kept;
            std::vector<AngularSpan> spans;
            const double tol = 1.5 * spacing / r;
            for (Polyline &pl : pieces) {
                AngularSpan sp = angular_span(pl, c);
                bool supported = rings.empty() ||
                    std::any_of(prev_spans.begin(), prev_spans.end(), [&](const AngularSpan &o) { return spans_overlap(sp, o, tol); });
                if (supported) {
                    spans.push_back(sp);
                    kept.emplace_back(std::move(pl));
                }
            }
            if (kept.empty()) {
                // Around a branch center the first rings may still lie inside the already filled area.
                if (rings.empty() && ++empty_start <= 3)
                    continue;
                break;
            }
            rings.emplace_back(std::move(kept));
            prev_spans = std::move(spans);
            r_last     = r;
        }
        return r_last;
    };

    // Mark the area covered by the arcs of one center (stroke width ~ spacing) as filled.
    auto subtract = [&](const Point &c, double r0, const std::vector<Polylines> &rings) {
        Polylines all;
        for (const Polylines &ring : rings)
            all.insert(all.end(), ring.begin(), ring.end());
        Polygons covered = offset(all, float(0.55 * spacing));
        covered.emplace_back(disk(c, r0, p.max_segment_length));
        remaining = diff_ex(remaining, covered);
    };

    // Emit the rings of one center: greedy nearest end, so consecutive arcs zig-zag instead of
    // travelling back over the full arc.
    auto emit = [&](std::vector<Polylines> &rings) {
        for (Polylines &ring : rings) {
            while (!ring.empty()) {
                size_t best = 0;
                bool   rev  = false;
                if (!result.arcs.empty()) {
                    const Point &last = result.arcs.back().last_point();
                    double       bd   = std::numeric_limits<double>::max();
                    for (size_t i = 0; i < ring.size(); ++i) {
                        double d0 = (ring[i].first_point() - last).cast<double>().squaredNorm();
                        double d1 = (ring[i].last_point() - last).cast<double>().squaredNorm();
                        if (d0 < bd) { bd = d0; best = i; rev = false; }
                        if (d1 < bd) { bd = d1; best = i; rev = true; }
                    }
                }
                Polyline pl = std::move(ring[best]);
                ring.erase(ring.begin() + best);
                if (rev) pl.reverse();
                result.arcs.emplace_back(std::move(pl));
            }
        }
    };

    struct Center { Point c; double r_last; };
    std::vector<Center>    finals;
    std::vector<Polylines> rings;

    // First center on the starting edge.
    {
        Points candidates = start_candidates(start_lines, p.spacing);
        if (p.avoid_start)
            std::stable_sort(candidates.begin(), candidates.end(), [&p](const Point &a, const Point &b) {
                return (a - p.avoid_point).cast<double>().squaredNorm() > (b - p.avoid_point).cast<double>().squaredNorm();
            });
        bool         found      = false;
        for (size_t i = 0; i < candidates.size() && i < 24 && !found; ++i) {
            double r_last = grow(candidates[i], double(p.r_min_start), rings);
            if (r_last > 0. && rings.size() >= p.min_start_arcs) {
                subtract(candidates[i], double(p.r_min_start), rings);
                emit(rings);
                finals.push_back({ candidates[i], r_last });
                result.start_point = candidates[i];
                found = true;
            }
        }
        if (!found)
            return result;
    }

    // Breadth-first: keep branching new centers off the outermost arc of every center until no point
    // on it is farther than max_dist_from_perimeter from the region boundary.
    const double region_area   = region.area();
    double       center_offset = double(p.center_offset);
    double       r_min         = double(p.r_min);
    bool         tried_fix     = false;
    size_t       idx           = 0;
    size_t       n_centers     = 1;
    while (idx < finals.size() && n_centers < p.max_centers && !remaining.empty()) {
        const Center cur = finals[idx];
        // Probe just outside the outermost arc: the parts of that probe ring that are still in the
        // remaining space mark arc points with unfilled area beyond them.
        Polylines outside = intersection_pl(circle_polyline(cur.c, cur.r_last + 0.8 * spacing, p.max_segment_length), remaining);
        double best_d = -1.;
        Point  best_p;
        for (const Polyline &pl : outside)
            for (const Point &q : pl.points) {
                Vec2d v   = (q - cur.c).cast<double>();
                double len = v.norm();
                if (len < 1.)
                    continue;
                v *= cur.r_last / len;
                Point  pt(cur.c.x() + coord_t(std::lround(v.x())), cur.c.y() + coord_t(std::lround(v.y())));
                double d = distance_to_lines(pt, boundary);
                if (d > best_d) { best_d = d; best_p = pt; }
            }
        if (best_d < 0. || best_d < double(p.max_dist_from_perimeter)) {
            ++idx;
            continue;
        }
        const Point nc = move_toward(best_p, cur.c, std::min(center_offset, cur.r_last));
        double r_last = grow(nc, r_min, rings);
        ++n_centers;
        if (r_last > 0.) {
            subtract(nc, r_min, rings);
            emit(rings);
            finals.push_back({ nc, r_last });
        } else
            ++idx;

        // The first branch got stuck in a narrow spot: retry with centers right on the arc.
        if (!tried_fix && finals.size() == 1 && idx == 1 && total_area(remaining) > 0.5 * region_area) {
            center_offset = 0.;
            r_min         = spacing / 1.5;
            idx           = 0;
            tried_fix     = true;
        }
    }
    return result;
}

} // namespace Slic3r
