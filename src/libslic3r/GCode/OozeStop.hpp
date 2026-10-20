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
// printing on every layer: the nozzle retracts, waits (parked over the purge chute, or
// hovering just above the restart point) and restarts with a chosen extra length of
// filament in place of retract_restart_extra. Printing the same stop with different
// extra lengths shows how much material a large nozzle loses while it waits, which is
// the reprime a real print needs after a stop of that length.
//
// The settings live in the object name so they survive saving the project and can be
// typed onto any object. Tokens are case-insensitive and separated by spaces or
// underscores:
//   W<s>              wait time in seconds (required)
//   R<mm>             extra filament pushed on restart, mm of filament (default 0)
//   R<mm>+<step>/<n>  the same, raised by <step> every <n> layers of the object
//   C<deg>            cool the nozzle by <deg> while parked (default 20 parked, 0 hovering)
//   H                 hover above the restart point instead of parking
//   P                 purge R into the chute, then restart with the normal extra length
// Examples: "ooze W30 R1.2", "ooze W20 R0+0.5/3", "ooze W60 R1 C0 H".
struct OozeStopSpec
{
    double wait_s       = 0.;
    double reprime      = 0.;
    double reprime_step = 0.;
    int    step_layers  = 0;  // 0: no ramp
    int    cool         = -1; // -1: default for the stop type
    bool   hover        = false;
    bool   purge        = false;

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
std::string ooze_dwell_gcode(double seconds, bool bambu_firmware);

// ---- Ooze & reprime calibration plate ----------------------------------------------
//
// Ooze ladder: one row of cylinders per wait time, one column per reprime length. Every
//   layer of every cylinder starts after a stop, so each seam shows whether that reprime
//   refills what the nozzle lost during that wait (a gap: too little; a blob: too much).
// Reprime check: one row of taller cylinders, all with the same wait, around one reprime
//   length, to confirm a value from the ladder over many layers.
struct OozeCalibParams
{
    bool                ladder        = true;
    bool                hover         = false; // hover over the part instead of parking at the chute
    bool                purge         = false; // reprime check only: purge into the chute, restart normally
    int                 cool          = 20;    // degrees to cool by while parked
    // Ooze ladder
    std::vector<double> waits         {5., 10., 20., 40., 80.};
    double              reprime_start = 0.;
    double              reprime_step  = 0.5;
    int                 reprime_count = 7;
    int                 ladder_layers = 2;
    // Reprime check
    double              wait          = 30.;
    double              reprime_mid   = 1.;
    double              check_step    = 0.5;
    int                 boxes         = 5;
    int                 check_layers  = 12;
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
    double brim       = 0.; // brim width around each cylinder
    int    layers     = 0;
    int    stops      = 0;  // stops in the whole print
    double wait_total = 0.; // seconds spent waiting
};

// Places the test cylinders on a bed spanning bed_min..bed_max. Returns nullopt and sets
// *error (translated) when the parameters are invalid or the plate does not fit.
std::optional<OozeCalibLayout> ooze_calib_layout(const OozeCalibParams &params, const Vec2d &bed_min, const Vec2d &bed_max,
                                                 std::string *error);

} // namespace Slic3r
