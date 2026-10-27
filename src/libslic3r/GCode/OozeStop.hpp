#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r {

// Ooze & reprime test objects (OrcaGarageBuild).
//
// An object whose name starts with the word "ooze" gets a timed stop before it starts
// printing on every layer: the nozzle retracts, waits (over its own ooze tower, parked over
// the purge chute, or hovering just above the restart point) and restarts with a chosen
// extra length of filament in place of retract_restart_extra. Printing the same stop with
// different extra lengths shows how much material a large nozzle loses while it waits,
// which is the reprime a real print needs after a stop of that length.
//
// The settings live in the object name so they survive saving the project and can be
// typed onto any object. Tokens are case-insensitive and separated by spaces or
// underscores:
//   W<s>              wait time in seconds (required)
//   R<mm>             extra filament pushed on restart, mm of filament (default 0)
//   R<mm>+<step>/<n>  the same, raised by <step> every <n> layers of the object
//   C<deg>            cool the nozzle by <deg> while waiting (default 20 parked or over a
//                     tower, 0 hovering)
//   D<mm>             retract this much further while waiting, pushed back on restart
//   A                 in place of R: reprime what the ooze curve in the print settings
//                     (ooze_reprime_max, ooze_reprime_half_life) gives for the time the nozzle
//                     actually spends off the object, from its retraction to its restart
//   B<mm>             start boost: after the reprime, push this much more filament along the
//                     first ooze_reprime_boost_distance of the line, tapering off, so the
//                     pressure builds while the nozzle moves instead of in a blob
//   S<s>              after pushing the reprime, wait this long for the pressure to build
//                     before printing (default 0)
//   T                 print the layer of an ooze tower behind the object first, then wait over
//                     its hollow and wipe across it before restarting (see OozeTower.hpp)
//   H                 hover above the restart point instead
//   P                 purge R into the chute or the tower, then restart with the normal
//                     extra length
// Without T or H the nozzle parks over the purge chute of a Bambu Lab X1 or P1.
// Examples: "ooze W30 R1.2 S1 T", "ooze W20 R0+0.5/3 C40 D2 T", "ooze W60 R1 C0 H", "ooze W4 A B2 T".
struct OozeStopSpec
{
    double wait_s        = 0.;
    double reprime       = 0.;
    double reprime_step  = 0.;
    int    step_layers   = 0;  // 0: no ramp
    bool   auto_reprime  = false; // A: reprime from the ooze curve, at the real time off the object
    double boost         = 0.;    // B: start boost, mm of filament
    int    cool          = -1; // -1: default for the stop type
    double extra_retract = 0.;
    double restart_dwell = 0.; // seconds between the reprime and the first move
    bool   tower         = false;
    bool   hover         = false;
    bool   purge         = false;

    // Extra length for a 0-based layer of the object.
    double reprime_at_layer(int object_layer) const;
    // Degrees to cool the nozzle by while waiting.
    int    cool_deg() const { return cool >= 0 ? cool : (hover ? 0 : 20); }
};

// Returns nullopt for names that don't start with the word "ooze". Throws
// Slic3r::SlicingError when the name starts with it but has no wait time.
std::optional<OozeStopSpec> parse_ooze_object_name(const std::string &name);

// Object name for a spec, in the form parse_ooze_object_name() reads.
std::string ooze_object_name(const OozeStopSpec &spec);

// Bambu Lab X1 and P1 printers have a purge chute behind the bed and a silicone wiper.
bool printer_has_bambu_purge_chute(const std::string &printer_model);

// Chute position, from Bambu's own filament-change G-code: approach at Y245, then Y265.
struct OozeChute
{
    double x;
    double approach_y;
    double y;
};
inline constexpr OozeChute bambu_purge_chute{70., 245., 265.};

// Bambu's shake-and-wipe after a purge: moves along X at the chute's Y.
struct OozeWipeMove
{
    double x;
    double feedrate; // mm/min
};
inline constexpr std::array<OozeWipeMove, 10> bambu_wipe_moves{{
    {76., 15000.}, {65., 15000.}, {76., 15000.}, {65., 15000.}, {80., 6000.},
    {95., 15000.}, {80., 15000.}, {100., 15000.}, {70., 15000.}, {100., 5000.},
}};

// G-code that waits for the moves to finish and then for the given time. Bambu
// firmware dwells with M400 S<s> / M400 P<ms>, as Bambu's own G-code does.
std::string ooze_dwell_gcode(double seconds, bool bambu_firmware, const char *comment = "ooze wait");

// ---- Ooze curve and start boost -----------------------------------------------------
//
// A nozzle left retracted oozes quickly at first and then less and less, until it has lost about all
// it can: the reprime that puts back what it lost after t seconds is
//     max * (1 - 0.5^(t / half_life)).
// The ooze ladder measures both numbers. A half-life of 0 or less means the nozzle loses the maximum
// at once.
double ooze_curve_reprime(double max_mm, double half_life_s, double seconds);

// A reprime pushed in place refills the nozzle, but a large nozzle still starts its line thin while
// the pressure builds. The start boost pushes more filament along the first part of the line instead:
// most at the restart point, tapering to nothing at the boost distance. Returns the share of the boost
// that goes between from_mm and to_mm along the line (0..1).
double ooze_boost_share(double from_mm, double to_mm, double distance_mm);

// Seconds for a move that starts and ends at rest: it accelerates at accel up to speed, cruises and
// brakes. accel <= 0 means no acceleration limit.
double ooze_move_time(double distance, double speed, double accel);

// Seconds a piece of G-code takes to run, for timing how long the nozzle is off the print. Reads
// G0/G1 moves (each from rest to rest), M204 accelerations, SET_VELOCITY_LIMIT, and G4 and M400 dwells.
// start is the position in the G-code's own coordinates; e moves count only when they move no axis.
struct OozeGCodeClock
{
    Vec3d  position     = Vec3d::Zero();
    double print_accel  = 0.; // acceleration of extruding moves; 0: none known
    double travel_accel = 0.; // acceleration of travel moves; 0: same as print_accel
    double feedrate     = 0.; // mm/min
    bool   relative_e   = true;
    double e            = 0.; // extruder position, with absolute E
};
double ooze_gcode_time(const std::string &gcode, OozeGCodeClock clock);

// ---- Ooze & reprime calibration plate ----------------------------------------------
//
// Ooze ladder: one row of cylinders per wait time, one column per reprime rate. A cylinder's
//   reprime is the base reprime plus its column's rate times its row's wait, so each column
//   tests one ooze rate at every wait. Every layer of every cylinder starts after a stop, so
//   each seam shows whether that reprime refills what the nozzle lost during that wait (a
//   gap: too little; a blob: too much).
// Reprime check: one row of taller cylinders, all with the same wait, around one reprime
//   length, to confirm a value from the ladder over many layers.
// Boost ladder: one row per wait, one column per start boost. Every cylinder reprimes what the ooze
//   curve gives for its real time off (A), so the columns show which boost fills the start of the
//   line without a blob.
enum class OozeStopKind
{
    Tower, // wait over an ooze tower behind each cylinder
    Chute, // park over the purge chute
    Hover, // hover over the part, as during a long travel
};

enum class OozeCalibTest
{
    Ladder,
    Check,
    Boost,
};

struct OozeCalibParams
{
    OozeCalibTest       test          = OozeCalibTest::Ladder;
    OozeStopKind        stop          = OozeStopKind::Tower;
    bool                purge         = false; // reprime check only: purge into the chute or tower, restart normally
    // Waits are like travels by default: no cooling and the normal retraction.
    int                 cool          = 0;     // degrees to cool by while waiting over a tower or the chute
    double              extra_retract = 0.;    // retracted further while waiting
    double              restart_dwell = 0.;    // seconds to let the pressure build after the reprime
    // Diameter of each tower's footprint, brim included; needed for Tower stops.
    double              tower_footprint = 0.;
    // Ooze ladder
    std::vector<double> waits         {5., 15., 30., 60.};
    double              reprime_base  = 3.;  // mm pushed after any wait
    double              rate_start    = 0.1; // mm of filament per second of wait, first column
    double              rate_step     = 0.1;
    int                 reprime_count = 6;   // columns
    int                 ladder_layers = 4;
    // Reprime check
    double              wait          = 30.;
    double              reprime_mid   = 2.5;
    double              check_step    = 0.5;
    int                 boxes         = 5;
    int                 check_layers  = 12;
    // Boost ladder; the ooze curve and the boost distance go into the print settings.
    std::vector<double> boost_waits   {1., 4., 10.};
    double              ooze_max      = 2.;  // mm of filament
    double              half_life     = 5.;  // seconds
    double              boost_start   = 0.;  // mm of filament, first column
    double              boost_step    = 1.;
    int                 boost_count   = 5;   // columns
    double              boost_distance = 15.; // mm along the line
    int                 boost_layers  = 12;
};

struct OozeCalibObject
{
    std::string name;
    Vec2d       center; // bed coordinates
};

struct OozeCalibLayout
{
    std::vector<OozeCalibObject> objects;
    double diameter   = 0.; // cylinder diameter
    double brim       = 0.; // room kept around each cylinder (the cylinders print without a brim)
    double tower_dy   = 0.; // distance from a cylinder's centre to its tower's, 0 without towers
    int    layers     = 0;
    int    stops      = 0;  // stops in the whole print
    double wait_total = 0.; // seconds spent waiting
};

// Places the test cylinders on a bed spanning bed_min..bed_max. Returns nullopt and sets
// *error (translated) when the parameters are invalid or the plate does not fit.
std::optional<OozeCalibLayout> ooze_calib_layout(const OozeCalibParams &params, const Vec2d &bed_min, const Vec2d &bed_max,
                                                 std::string *error);

} // namespace Slic3r
