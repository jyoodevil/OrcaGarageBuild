#include "OozeTower.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/Flow.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

std::vector<double> OozeTowerShape::loop_radii(bool first_layer) const
{
    std::vector<double> radii;
    const double inner = inner_radius();
    for (int i = 0; i < walls; ++i)
        radii.push_back(inner + (i + 0.5) * spacing);
    if (first_layer)
        for (int i = 0; i < brim_loops; ++i)
            radii.push_back(outer_radius() + (i + 0.5) * spacing);
    return radii;
}

OozeTowerShape make_ooze_tower_shape(double diameter, int walls, int brim_loops, double nozzle_diameter, double layer_height)
{
    OozeTowerShape shape;
    shape.diameter   = diameter;
    shape.walls      = std::max(0, walls);
    shape.brim_loops = std::max(0, brim_loops);
    shape.line_width = 1.05 * nozzle_diameter;
    shape.spacing    = Flow::rounded_rectangle_extrusion_spacing(float(shape.line_width), float(layer_height));
    // Keep a hollow at least one line wide, for the drips to fall into.
    if (shape.walls < 1 || shape.inner_radius() < shape.spacing)
        shape.walls = 0;
    return shape;
}

std::vector<Vec2d> ooze_tower_loop(const Vec2d &center, double radius, double start_angle)
{
    // Segments of about 1.5 mm, and never fewer than 16.
    const int          segments = std::max(16, int(std::ceil(2. * PI * radius / 1.5)));
    std::vector<Vec2d> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = start_angle + 2. * PI * double(i % segments) / double(segments);
        points.emplace_back(center + radius * Vec2d(std::cos(angle), std::sin(angle)));
    }
    return points;
}

double ooze_distance_to_box(const Vec2d &point, const Vec2d &box_min, const Vec2d &box_max)
{
    const double dx = std::max({box_min.x() - point.x(), 0., point.x() - box_max.x()});
    const double dy = std::max({box_min.y() - point.y(), 0., point.y() - box_max.y()});
    return std::hypot(dx, dy);
}

void OozeTower::start_layer(double print_z)
{
    if (m_layers > 0 && print_z <= m_layer_z + EPSILON)
        return;
    if (m_layers > 0)
        m_top_z = m_layer_z;
    m_layer_z = print_z;
    ++m_layers;
    m_loops = m_shape.loop_radii(m_layers == 1);
    m_next  = 0;
}

double OozeTower::next_loop_radius() const
{
    if (m_next < m_loops.size())
        return m_loops[m_next];
    return m_shape.outer_radius() - 0.5 * m_shape.spacing;
}

std::vector<double> OozeTower::take_loops(size_t count)
{
    const size_t end = std::min(m_loops.size(), m_next + count);
    std::vector<double> radii(m_loops.begin() + m_next, m_loops.begin() + end);
    m_next = end;
    return radii;
}

} // namespace Slic3r
