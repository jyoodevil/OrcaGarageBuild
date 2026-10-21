#pragma once

#include <cstddef>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r {

// Ooze towers (OrcaGarageBuild).
//
// A large nozzle drips while it waits or makes a long travel, and whatever hangs from it lands on
// the part when it comes back. An ooze tower is a hollow round tower next to the part that takes
// that material instead: the nozzle waits over its hollow centre, so the drips fall inside, then
// prints the tower's loops for the layer, which wipes off what still hangs from the nozzle and
// builds the pressure back up before the nozzle goes back to the part.
//
// The G-code prints the tower itself (it is not a model object), one layer of it whenever the part
// it belongs to prints a layer. Its first layer adds brim loops outside the walls, so it holds on
// to the bed when the nozzle drags ooze across it.
//
// Ooze test objects with the T token each get a tower right behind them (see OozeStop.hpp). A print
// can also have one tower for all its long travels (the ooze_tower options).
struct OozeTowerShape
{
    double diameter   = 0.; // outer diameter of the walls
    int    walls      = 0;
    int    brim_loops = 0;  // loops added outside the walls on the first layer
    double line_width = 0.;
    double spacing    = 0.; // distance between the centre lines of neighbouring loops

    double outer_radius() const { return 0.5 * diameter; }
    // Radius of the hollow inside the walls.
    double inner_radius() const { return outer_radius() - walls * spacing; }
    // Radius of the bed area the tower covers, brim included.
    double footprint_radius() const { return outer_radius() + brim_loops * spacing; }
    // Centre-line radii of the loops of one layer, inside out. The first layer adds the brim loops.
    std::vector<double> loop_radii(bool first_layer) const;
};

// Tower walls are 1.05 nozzle diameters wide, spaced for the given layer height. Returns a shape
// with walls == 0 when the walls leave no hollow inside.
OozeTowerShape make_ooze_tower_shape(double diameter, int walls, int brim_loops, double nozzle_diameter, double layer_height);

// Clearance between an object's brim and the brim of the tower behind it.
inline constexpr double ooze_tower_clearance = 3.;
// The nozzle waits this far above the top of the tower, over its hollow centre.
inline constexpr double ooze_tower_hover_height = 2.;

// Points of a loop around center, starting and ending at start_angle (radians), counterclockwise.
std::vector<Vec2d> ooze_tower_loop(const Vec2d &center, double radius, double start_angle);

// Distance from a point to an axis-aligned box, 0 inside it.
double ooze_distance_to_box(const Vec2d &point, const Vec2d &box_min, const Vec2d &box_max);

// One tower and how far it has been printed. Coordinates are print coordinates in mm.
class OozeTower
{
public:
    OozeTower(const Vec2d &center, const OozeTowerShape &shape) : m_center(center), m_shape(shape) {}

    const Vec2d          &center() const { return m_center; }
    const OozeTowerShape &shape() const { return m_shape; }

    // Starts the tower layer at print_z, unless it has started already. The first layer started
    // is the tower's first layer, with its brim.
    void start_layer(double print_z);
    double layer_z() const { return m_layer_z; }
    // Height of the layer being printed: from the top of the previous layer, or from the bed.
    double layer_height() const { return m_layer_z - m_top_z; }
    // Top of the tower below the layer being printed.
    double top_z() const { return m_top_z; }
    bool   first_layer() const { return m_layers == 1; }
    size_t loops_left() const { return m_loops.size() - m_next; }
    // Radius of the next loop to print, or of the outermost wall when the layer is done.
    double next_loop_radius() const;
    // Takes up to count loops of the layer, inside out, and returns their radii.
    std::vector<double> take_loops(size_t count);

private:
    Vec2d               m_center;
    OozeTowerShape      m_shape;
    double              m_layer_z = 0.;
    double              m_top_z   = 0.;
    int                 m_layers  = 0;
    std::vector<double> m_loops;
    size_t              m_next = 0;
};

} // namespace Slic3r
