#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/OozeTower.hpp"

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

// A 4 mm cube at 0.4 mm layers is 10 layers, each one a ring of walls that starts with one restart.
constexpr int    ooze_layers      = 10;
constexpr double ooze_retraction  = 2.;

DynamicPrintConfig ooze_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.8"},
        {"layer_height", 0.4},
        {"initial_layer_print_height", 0.4},
        {"wall_loops", 2},
        {"top_shell_layers", 0},
        {"bottom_shell_layers", 0},
        {"sparse_infill_density", "0%"},
        {"skirt_loops", 0},
        {"brim_type", "no_brim"},
        {"enable_prime_tower", 0},
        {"retraction_length", ooze_retraction},
        {"retract_restart_extra", 0},
        {"use_relative_e_distances", 1},
        {"use_firmware_retraction", 0},
        {"nozzle_temperature_initial_layer", 220},
        {"nozzle_temperature", 220},
    });
    return config;
}

// Where slice_ooze_cube() puts the cube's centre on the 200 mm bed, with room behind it for a tower.
const Vec2d ooze_cube_center(100., 80.);

// Moves an object's first copy so its bounding box is centred on center, leaving its height alone.
void place_on_bed(ModelObject &object, const Vec2d &center)
{
    ModelInstance      *instance = object.instances.front();
    const BoundingBoxf3 box      = object.instance_bounding_box(0);
    instance->set_offset(instance->get_offset() + Vec3d(center.x() - box.center().x(), center.y() - box.center().y(), 0.));
    object.invalidate_bounding_box();
}

// Role tags of the given extrusion role: "; FEATURE: <role>" on Bambu printers, ";TYPE:<role>" elsewhere.
size_t count_role_tags(const std::string &gcode, const std::string &role)
{
    size_t             n = 0;
    std::istringstream in(gcode);
    std::string        line;
    while (std::getline(in, line))
        if (line == "; FEATURE: " + role || line == ";TYPE:" + role)
            ++n;
    return n;
}

std::string slice_ooze_cube(const std::string &name, DynamicPrintConfig config, bool bambu_x1)
{
    if (bambu_x1)
        config.set_deserialize_strict({{"printer_model", "Bambu Lab X1 Carbon"}});
    Print print;
    print.is_BBL_printer() = bambu_x1;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(4.)}, print, model, config);
    model.objects.front()->name = name;
    place_on_bed(*model.objects.front(), ooze_cube_center);
    print.apply(model, config);
    return Test::gcode(print);
}

// Whether a line moves the nozzle or the filament (G0 to G3).
bool is_move(const std::string &line)
{
    return line.size() > 2 && line[0] == 'G' && line[1] >= '0' && line[1] <= '3' && line[2] == ' ';
}

// Filament pushed back after each marker, as groups of positive E-only moves in a row (a restart can push the
// retraction and then the reprime), up to count groups per marker. Only another move ends a group: comments,
// waits and progress reports do not.
std::vector<std::vector<double>> push_groups(const std::string &gcode, const std::string &marker, size_t count)
{
    std::vector<std::vector<double>> out;
    std::istringstream               in(gcode);
    std::string                      line;
    bool                             in_group = false;
    while (std::getline(in, line)) {
        if (line.rfind(marker, 0) == 0) {
            out.emplace_back();
            in_group = false;
            continue;
        }
        if (out.empty())
            continue;
        std::vector<double> &groups = out.back();
        if (line.rfind("G1 E", 0) == 0 && std::stod(line.substr(4)) > 0.) {
            if (!in_group) {
                if (groups.size() >= count)
                    continue;
                groups.push_back(0.);
                in_group = true;
            }
            groups.back() += std::stod(line.substr(4));
        } else if (is_move(line))
            in_group = false;
    }
    return out;
}

size_t count_of(const std::string &text, const std::string &token)
{
    size_t n = 0;
    for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + token.size()))
        ++n;
    return n;
}

// Filament pushed back at the restart after each "; OOZE stop" marker, for stops that print nothing in between.
std::vector<double> restart_lengths(const std::string &gcode)
{
    std::vector<double> out;
    for (const std::vector<double> &groups : push_groups(gcode, "; OOZE stop", 1))
        if (!groups.empty())
            out.push_back(groups.front());
    return out;
}

} // namespace

TEST_CASE("An ooze object stops once per layer and restarts with its reprime ramp", "[OozeStop]")
{
    // Hover 5 s, reprime 1.5 mm rising by 0.5 mm every 2 layers.
    const std::string gcode = slice_ooze_cube("ooze W5 R1.5+0.5/2 H", ooze_config(), false);

    CHECK(count_of(gcode, "; OOZE stop") == ooze_layers);
    CHECK(count_of(gcode, "G4 P5000") == ooze_layers);

    const std::vector<double> restarts = restart_lengths(gcode);
    REQUIRE(restarts.size() == ooze_layers);
    for (int layer = 0; layer < ooze_layers; ++layer) {
        INFO("layer " << layer);
        CHECK_THAT(restarts[layer], WithinAbs(ooze_retraction + 1.5 + 0.5 * (layer / 2), 1e-4));
    }
}

TEST_CASE("Objects that are not ooze objects print without stops", "[OozeStop]")
{
    const std::string gcode = slice_ooze_cube("Cube", ooze_config(), false);
    CHECK(count_of(gcode, "; OOZE stop") == 0);
    CHECK(gcode.find("OOZE_POST_DONE") == std::string::npos);
}

TEST_CASE("An X1 parks over the purge chute, cools while it waits and wipes", "[OozeStop]")
{
    // A slow travel speed must not slow the trip to the chute, which isn't part of the wait.
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({{"travel_speed", 20}, {"initial_layer_travel_speed", 20}});
    const std::string gcode = slice_ooze_cube("ooze W5 R1 C10", config, true);

    CHECK(count_of(gcode, "; OOZE stop") == ooze_layers);
    CHECK(count_of(gcode, "M400 S5") == ooze_layers);
    CHECK(gcode.find(" Y265") != std::string::npos);
    // 250 mm/s, within the default 500 mm/s machine limit.
    CHECK(count_of(gcode, "X70 Y245 F15000") == ooze_layers);
    CHECK(count_of(gcode, "M104 S210") == ooze_layers);
    CHECK(count_of(gcode, "M109 S220") >= size_t(ooze_layers));
    // The OozePost.bat post-processor must leave the file alone.
    CHECK(gcode.find("; OOZE_POST_DONE") != std::string::npos);

    const std::vector<double> restarts = restart_lengths(gcode);
    REQUIRE(restarts.size() == ooze_layers);
    for (double e : restarts)
        CHECK_THAT(e, WithinAbs(ooze_retraction + 1., 1e-4));
}

TEST_CASE("A purge stop pushes the reprime into the chute and restarts normally", "[OozeStop]")
{
    const std::string gcode = slice_ooze_cube("ooze W5 R2 P", ooze_config(), true);

    // The first positive E-only move after the stop is the purge (retraction + 2 mm), the restart
    // at the part pushes back only the retraction.
    std::istringstream in(gcode);
    std::string        line;
    std::vector<double> pushes;
    bool after_stop = false;
    while (std::getline(in, line)) {
        if (line.rfind("; OOZE stop", 0) == 0) {
            after_stop = true;
            pushes.clear();
            continue;
        }
        if (after_stop && line.rfind("G1 E", 0) == 0 && std::stod(line.substr(4)) > 0.) {
            pushes.push_back(std::stod(line.substr(4)));
            if (pushes.size() == 2)
                break;
        }
    }
    REQUIRE(pushes.size() == 2);
    CHECK_THAT(pushes[0], WithinAbs(ooze_retraction + 2., 1e-4));
    CHECK_THAT(pushes[1], WithinAbs(ooze_retraction, 1e-4));
}

TEST_CASE("Parking an ooze object needs a printer with a purge chute", "[OozeStop]")
{
    CHECK_THROWS(slice_ooze_cube("ooze W5 R1", ooze_config(), false));
}

TEST_CASE("A tower stop prints the tower, waits over its hollow, wipes across it, then reprimes and settles",
          "[OozeStop][OozeTower]")
{
    // No purge chute needed. Retract 2 mm more while waiting, reprime 1 mm, then let the pressure build for 1.5 s.
    const std::string gcode = slice_ooze_cube("ooze W5 R1 D2 S1.5 T", ooze_config(), false);

    CHECK(count_of(gcode, "; OOZE stop") == ooze_layers);
    CHECK(count_of(gcode, "G4 P5000") == ooze_layers);
    CHECK(count_of(gcode, "G4 P1500") == ooze_layers);
    // One layer of the tower per stop.
    CHECK(count_role_tags(gcode, "Prime tower") == ooze_layers);

    // Per stop: where the nozzle waits, where it goes next (the wipe), and the order of events after the stop:
    // t = tower loops, w = the wait, r = the restart pushes, s = the settle.
    struct Stop
    {
        Vec2d               wait;
        Vec2d               wipe;
        bool                wiped = false;
        std::string         events;
        std::vector<double> restart; // the pushes of the second group, at the restart point
    };
    std::vector<Stop>  stops;
    std::istringstream in(gcode);
    std::string        line;
    double             x = 0., y = 0.;
    int                groups   = 0;
    bool               in_group = false;
    while (std::getline(in, line)) {
        if (line.rfind("; OOZE stop", 0) == 0) {
            stops.emplace_back();
            groups   = 0;
            in_group = false;
            continue;
        }
        if (stops.empty())
            continue;
        Stop &stop = stops.back();
        if (line == "; FEATURE: Prime tower" || line == ";TYPE:Prime tower")
            stop.events += 't';
        if (line.rfind("G4 P5000", 0) == 0) {
            stop.wait = Vec2d(x, y);
            stop.events += 'w';
        }
        if (line.rfind("G4 P1500", 0) == 0)
            stop.events += 's';
        if (line.rfind("G1 E", 0) == 0 && std::stod(line.substr(4)) > 0.) {
            if (!in_group) {
                in_group = true;
                if (++groups == 2)
                    stop.events += 'r';
            }
            if (groups == 2)
                stop.restart.push_back(std::stod(line.substr(4)));
            continue;
        }
        if (is_move(line))
            in_group = false;
        if (line.rfind("G1 ", 0) != 0)
            continue;
        const size_t at_x = line.find(" X"), at_y = line.find(" Y");
        if (at_x != std::string::npos)
            x = std::stod(line.substr(at_x + 2));
        if (at_y != std::string::npos)
            y = std::stod(line.substr(at_y + 2));
        if (!stop.wiped && stop.events == "tw" && (at_x != std::string::npos || at_y != std::string::npos)) {
            stop.wipe  = Vec2d(x, y);
            stop.wiped = true;
        }
    }
    REQUIRE(stops.size() == ooze_layers);
    for (const Stop &stop : stops) {
        INFO("events " << stop.events);
        CHECK(stop.events.rfind("twrs", 0) == 0);
        // The nozzle waits behind the cube, clear of it, and wipes across the tower's wall towards the cube.
        CHECK_THAT(stop.wait.x(), WithinAbs(ooze_cube_center.x(), 1e-3));
        CHECK(stop.wait.y() > ooze_cube_center.y() + 2. + ooze_tower_clearance);
        CHECK(stop.wipe.y() < stop.wait.y() - 12.5);
        CHECK(stop.wipe.y() > ooze_cube_center.y() + 2.);
        // At the restart point the retraction and the extra 2 mm go back first, then the reprime on its own.
        REQUIRE(stop.restart.size() == 2);
        CHECK_THAT(stop.restart[0], WithinAbs(ooze_retraction + 2., 1e-4));
        CHECK_THAT(stop.restart[1], WithinAbs(1., 1e-4));
    }
}

TEST_CASE("An ooze tower that does not fit on the bed is an error", "[OozeStop][OozeTower]")
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({{"ooze_tower_diameter", 300}});
    CHECK_THROWS(slice_ooze_cube("ooze W5 R1 T", config, false));
}

namespace {

// Ooze curve for these tests: at most 4 mm, half of it every 2 s.
constexpr double ooze_max       = 4.;
constexpr double ooze_half_life = 2.;

double curve(double seconds) { return ooze_max * (1. - std::exp2(-seconds / ooze_half_life)); }

// Settings for reprime by the time off the print, with a start boost of the given length over 4 mm.
void set_ooze_reprime(DynamicPrintConfig &config, bool reprime, double boost)
{
    config.set_deserialize_strict({
        {"ooze_reprime", reprime ? 1 : 0},
        {"ooze_reprime_max", ooze_max},
        {"ooze_reprime_half_life", ooze_half_life},
        {"ooze_reprime_min_time", 1},
        {"ooze_reprime_boost", boost},
        {"ooze_reprime_boost_distance", 4},
        {"ooze_reprime_settle", 0.5},
    });
}

// Two 4 mm cubes 140 mm apart at 50 mm/s travel and 500 mm/s^2, so a trip between them takes at least
// 140 / 50 + 50 / 500 = 2.9 s, optionally with the print's ooze tower between and behind them, draining for 2 s,
// and with reprime by the time off the print. One wall makes every line after a restart a loop of about 12 mm.
std::string slice_two_cubes(bool tower, bool reprime, double boost = 0., int walls = 2)
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({
        {"wall_loops", walls},
        {"travel_speed", 50},
        {"initial_layer_travel_speed", 50},
        {"default_acceleration", 500},
        {"travel_acceleration", 500},
        {"initial_layer_travel_acceleration", 500},
        {"inner_wall_speed", 20},
        {"outer_wall_speed", 20},
        {"initial_layer_speed", 20},
        {"filament_max_volumetric_speed", 10},
        {"ooze_tower", tower ? 1 : 0},
        {"ooze_tower_x", 100},
        {"ooze_tower_y", 140},
        {"ooze_tower_travel_time", 2},
        {"ooze_tower_drain", 2},
    });
    set_ooze_reprime(config, reprime, boost);
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(4.), Test::cube(4.)}, print, model, config);
    const double x[] = {30., 170.};
    for (size_t i = 0; i < 2; ++i)
        place_on_bed(*model.objects[i], Vec2d(x[i], 100.));
    print.apply(model, config);
    return Test::gcode(print);
}

// The "; OOZE reprime: <t> s off the print, reprime <r> mm" notes, and the first push after each.
struct ReprimeNote
{
    double time;
    double reprime;
    double pushed = 0.;
};
std::vector<ReprimeNote> reprime_notes(const std::string &gcode, const char *format = "; OOZE reprime: %lf s off the print, reprime %lf mm")
{
    std::vector<ReprimeNote> out;
    std::istringstream       in(gcode);
    std::string              line;
    bool                     want_push = false;
    while (std::getline(in, line)) {
        double t = 0., r = 0.;
        if (std::sscanf(line.c_str(), format, &t, &r) == 2) {
            out.push_back({t, r});
            want_push = true;
        } else if (want_push && line.rfind("G1 E", 0) == 0) {
            out.back().pushed = std::stod(line.substr(4));
            want_push         = false;
        }
    }
    return out;
}

// The 40 lines after the first marker, on one line, to show in a failed check.
std::string after_first(const std::string &gcode, const std::string &marker)
{
    const size_t at = gcode.find(marker);
    if (at == std::string::npos)
        return "(no " + marker + ")";
    std::istringstream in(gcode.substr(at));
    std::string        line, out;
    for (int i = 0; i < 40 && std::getline(in, line); ++i)
        out += line + " | ";
    return out;
}

// The line that starts after each marker: the filament pushed over its first `distance` mm beyond the line's own
// flow (taken from the rest of the line), and the highest volumetric flow there, in mm^3/s.
struct BoostedStart
{
    double extra    = 0.;
    double max_flow = 0.;
};
std::vector<BoostedStart> boosted_starts(const std::string &gcode, const std::string &marker, double distance)
{
    const double filament_area = 0.25 * PI * 1.75 * 1.75;
    struct Piece
    {
        double length, e, feedrate;
    };
    std::vector<BoostedStart> out;
    std::vector<Piece>        pieces;
    bool                      active = false;
    const auto finish = [&]() {
        active = false;
        // The line's own flow, from the part past the boost.
        double at = 0., tail_length = 0., tail_e = 0.;
        for (const Piece &piece : pieces) {
            if (at >= distance - 1e-6) {
                tail_length += piece.length;
                tail_e += piece.e;
            }
            at += piece.length;
        }
        if (tail_length < 0.5)
            return;
        BoostedStart start;
        at = 0.;
        for (const Piece &piece : pieces) {
            if (at < distance - 1e-6) {
                start.extra += piece.e - tail_e / tail_length * piece.length;
                start.max_flow = std::max(start.max_flow, piece.e * filament_area / piece.length * piece.feedrate / 60.);
            }
            at += piece.length;
        }
        out.push_back(start);
    };
    std::istringstream in(gcode);
    std::string        line;
    double             x = 0., y = 0., f = 0.;
    while (std::getline(in, line)) {
        if (line.rfind(marker, 0) == 0) {
            pieces.clear();
            active = true;
            continue;
        }
        if (line.rfind("G1 ", 0) != 0)
            continue;
        const std::string code = line.substr(0, line.find(';'));
        double            nx = x, ny = y, e = 0.;
        bool              has_e = false;
        std::istringstream words(code.substr(3));
        std::string        word;
        while (words >> word) {
            const double v = std::stod(word.substr(1));
            switch (word[0]) {
            case 'X': nx = v; break;
            case 'Y': ny = v; break;
            case 'E': e = v; has_e = true; break;
            case 'F': f = v; break;
            default: break;
            }
        }
        const double length = std::hypot(nx - x, ny - y);
        x = nx;
        y = ny;
        if (!active)
            continue;
        if (length > 1e-9 && has_e && e > 0.)
            pieces.push_back({length, e, f});
        else if (!pieces.empty() && ((length > 1e-9) || (has_e && e < 0.)))
            finish();
    }
    return out;
}

} // namespace

TEST_CASE("Long travels between parts go through the print's ooze tower, which grows every layer", "[OozeStop][OozeTower]")
{
    const std::string gcode = slice_two_cubes(true, false);

    // Each layer travels once between the cubes, and the rest of the tower's layer is printed at its end.
    CHECK(count_of(gcode, "; OOZE tower: visit") >= ooze_layers);
    CHECK(count_of(gcode, "; OOZE tower: rest of the layer") == ooze_layers);
    // Each visit lets the nozzle drain over the tower for the drain time.
    CHECK(count_of(gcode, "G4 P2000") == count_of(gcode, "; OOZE tower: visit"));

    // After the tower the part restarts with the reprime the ooze curve gives for the whole time away, the
    // drain included, even though reprime after travels is off. Then it settles.
    const std::vector<ReprimeNote> notes = reprime_notes(gcode);
    CHECK(notes.size() == count_of(gcode, "; OOZE tower: visit"));
    for (const ReprimeNote &note : notes) {
        INFO("away " << note.time << " s");
        CHECK(note.time > 2.);
        CHECK_THAT(note.reprime, WithinAbs(curve(note.time), 0.01));
        CHECK_THAT(note.pushed, WithinAbs(note.reprime, 1e-3));
    }
    CHECK(count_of(gcode, "G4 P500") >= count_of(gcode, "; OOZE tower: visit"));
}

TEST_CASE("Without the ooze tower option, travels go straight", "[OozeStop][OozeTower]")
{
    const std::string gcode = slice_two_cubes(false, false);
    CHECK(gcode.find("; OOZE tower") == std::string::npos);
    CHECK(count_role_tags(gcode, "Prime tower") == 0);
    CHECK(gcode.find("; OOZE reprime") == std::string::npos);
}

TEST_CASE("A restart after a long travel reprimes by the ooze curve for the time off the print", "[OozeStop][OozeReprime]")
{
    const std::string              gcode = slice_two_cubes(false, true);
    const std::vector<ReprimeNote> notes = reprime_notes(gcode);

    // Every travel between the cubes takes at least 2.9 s with its acceleration, plus its retraction and lift;
    // shorter travels inside a cube restart normally.
    size_t long_travels = 0;
    for (const ReprimeNote &note : notes) {
        INFO("travel " << note.time << " s");
        CHECK(note.time >= 1.);
        CHECK_THAT(note.reprime, WithinAbs(curve(note.time), 0.01));
        CHECK_THAT(note.pushed, WithinAbs(note.reprime, 1e-3));
        if (note.time > 2.9)
            ++long_travels;
    }
    CHECK(long_travels >= size_t(ooze_layers - 1));
    // Each reprime settles before the nozzle moves.
    CHECK(count_of(gcode, "G4 P500") == notes.size());
}

TEST_CASE("A start boost spreads extra filament over the start of the line, within the volumetric limit", "[OozeStop][OozeReprime]")
{
    // 1 mm of boost over the first 4 mm of each line after a reprime.
    const std::string               gcode  = slice_two_cubes(false, true, 1., 1);
    const std::vector<BoostedStart> starts = boosted_starts(gcode, "; OOZE reprime", 4.);
    INFO(after_first(gcode, "; OOZE reprime"));
    REQUIRE(starts.size() >= size_t(ooze_layers - 1));
    for (const BoostedStart &start : starts) {
        CHECK_THAT(start.extra, WithinAbs(1., 0.01));
        // The filament allows 10 mm^3/s; the boosted start slows down to stay within it.
        CHECK(start.max_flow <= 10.05);
    }

    // Without a boost, the start of the line is like the rest of it.
    const std::vector<BoostedStart> plain = boosted_starts(slice_two_cubes(false, true, 0., 1), "; OOZE reprime", 4.);
    CHECK(plain.size() == starts.size());
    for (const BoostedStart &start : plain)
        CHECK_THAT(start.extra, WithinAbs(0., 0.01));
}

TEST_CASE("A stop with A reprimes by the ooze curve for its real time off the object, then boosts", "[OozeStop]")
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({{"wall_loops", 1}, {"default_acceleration", 500}, {"outer_wall_acceleration", 300}, {"inner_wall_acceleration", 300},
                                   {"travel_acceleration", 500}, {"inner_wall_speed", 20}, {"outer_wall_speed", 20},
                                   {"initial_layer_speed", 20}, {"filament_max_volumetric_speed", 10}});
    set_ooze_reprime(config, false, 0.);
    const std::string gcode = slice_ooze_cube("ooze W3 A B0.8 T", config, false);

    const std::vector<ReprimeNote> notes =
        reprime_notes(gcode, "; OOZE restart: %lf s off the object | reprime %lf mm");
    REQUIRE(notes.size() == ooze_layers);
    for (const ReprimeNote &note : notes) {
        INFO("off " << note.time << " s");
        // The wait plus the moves over and off the tower.
        CHECK(note.time > 3.);
        CHECK(note.time < 10.);
        CHECK_THAT(note.reprime, WithinAbs(curve(note.time), 0.01));
    }
    // The restart pushes the retraction back, then that reprime.
    const std::vector<std::vector<double>> restarts = push_groups(gcode, "; OOZE restart", 1);
    REQUIRE(restarts.size() == ooze_layers);
    for (size_t i = 0; i < restarts.size(); ++i) {
        REQUIRE(restarts[i].size() == 1);
        CHECK_THAT(restarts[i].front(), WithinAbs(ooze_retraction + notes[i].reprime, 1e-3));
    }
    const std::vector<BoostedStart> starts = boosted_starts(gcode, "; OOZE restart", 4.);
    INFO(after_first(gcode, "; OOZE restart"));
    CHECK(starts.size() == size_t(ooze_layers));
    for (const BoostedStart &start : starts)
        CHECK_THAT(start.extra, WithinAbs(0.8, 0.01));

    // After the tower's walls, the trips to the hollow and back run faster than the walls were printed.
    std::istringstream in(gcode);
    std::string        line;
    int                accel = 0, after_tower = 0;
    bool               in_tower = false;
    std::vector<int>   wait_accels;
    while (std::getline(in, line)) {
        if (line == "; FEATURE: Prime tower" || line == ";TYPE:Prime tower")
            in_tower = true;
        if (line.rfind("M204 S", 0) == 0)
            accel = std::stoi(line.substr(6));
        if (in_tower && line.rfind("G4 P3000", 0) == 0) {
            wait_accels.push_back(accel);
            in_tower = false;
        }
    }
    REQUIRE(wait_accels.size() == size_t(ooze_layers));
    for (int a : wait_accels)
        CHECK(a > 300);
}

namespace {

// One object of two 12 mm square sections 40 mm apart, at 0.4 mm layers with one wall and sparse infill, so each
// layer jumps over 28 mm of open air between them at 20 mm/s and 500 mm/s^2, with reprime by the time off the print.
constexpr double section_size = 12.;
constexpr double section_gap  = 28.;
std::string slice_two_sections(std::initializer_list<ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({
        {"wall_loops", 1},
        {"sparse_infill_density", "25%"},
        {"sparse_infill_pattern", "rectilinear"},
        {"travel_speed", 20},
        {"initial_layer_travel_speed", 20},
        {"default_acceleration", 500},
        {"travel_acceleration", 500},
        {"initial_layer_travel_acceleration", 500},
    });
    set_ooze_reprime(config, true, 0.);
    config.set_deserialize_strict(items);
    TriangleMesh mesh  = Test::cube(section_size);
    TriangleMesh other = Test::cube(section_size);
    other.translate(float(section_size + section_gap), 0.f, 0.f);
    mesh.merge(other);
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{mesh}, print, model, config);
    place_on_bed(*model.objects.front(), Vec2d(100., 100.));
    print.apply(model, config);
    return Test::gcode(print);
}

// Per layer, the sections the extrusions go to in turn, 0 for the left one and 1 for the right one, with repeats
// merged.
std::vector<std::vector<int>> section_visits(const std::string &gcode)
{
    std::vector<std::vector<int>> layers;
    std::istringstream            in(gcode);
    std::string                   line;
    double                        x = 0.;
    while (std::getline(in, line)) {
        if (line == ";LAYER_CHANGE" || line == "; CHANGE_LAYER") {
            layers.emplace_back();
            continue;
        }
        if (!is_move(line) || layers.empty())
            continue;
        const std::string code = line.substr(0, line.find(';'));
        double            e    = 0.;
        bool              has_x = false;
        std::istringstream words(code);
        std::string        word;
        while (words >> word) {
            if (word[0] == 'X') {
                x     = std::stod(word.substr(1));
                has_x = true;
            } else if (word[0] == 'E')
                e = std::stod(word.substr(1));
        }
        if (!has_x || !(e > 0.))
            continue;
        const int side = x < 100. ? 0 : 1;
        if (layers.back().empty() || layers.back().back() != side)
            layers.back().push_back(side);
    }
    return layers;
}

} // namespace

TEST_CASE("Chained sections start each layer on the section the last layer ended on", "[OozeStop][OozeStrings]")
{
    const std::vector<std::vector<int>> layers = section_visits(slice_two_sections({{"ooze_chain_sections", 1}}));
    // 12 mm at 0.4 mm layers.
    REQUIRE(layers.size() == 30);
    for (size_t i = 0; i < layers.size(); ++i) {
        INFO("layer " << i);
        // Each section once per layer: a single jump between them.
        REQUIRE(layers[i].size() == 2);
        if (i > 0)
            CHECK(layers[i].front() == layers[i - 1].back());
    }
}

TEST_CASE("Before a jump over open air the nozzle wipes on the infill until little ooze is left for the jump",
          "[OozeStop][OozeStrings]")
{
    constexpr double  limit = 0.2;
    const std::string gcode = slice_two_sections({{"ooze_wipe_limit", limit}, {"ooze_wipe_speed", 10}});
    INFO(after_first(gcode, "; OOZE wipe"));

    // "; OOZE wipe: <s> s along <role> before <air> mm over open air, ooze on the way <left> mm instead of <was> mm",
    // then the wipe's moves, at layer height and inside the section it leaves, and the restart's note.
    struct Wipe
    {
        double seconds, air, left, was;
        double xmin = 1e9, xmax = -1e9;
        int    moves = 0;
        bool   moved_z = false, extruded = false;
        double off = -1.;
    };
    std::vector<Wipe>  wipes;
    std::istringstream in(gcode);
    std::string        line;
    bool               in_wipe = false;
    while (std::getline(in, line)) {
        Wipe         w{};
        const size_t before = line.find(" before ");
        if (line.rfind("; OOZE wipe:", 0) == 0 && before != std::string::npos &&
            std::sscanf(line.c_str(), "; OOZE wipe: %lf s", &w.seconds) == 1 &&
            std::sscanf(line.c_str() + before, " before %lf mm over open air, ooze on the way %lf mm instead of %lf mm",
                        &w.air, &w.left, &w.was) == 3) {
            wipes.push_back(w);
            in_wipe = true;
            continue;
        }
        double t = 0.;
        if (!wipes.empty() && wipes.back().off < 0. && std::sscanf(line.c_str(), "; OOZE reprime: %lf s off the print", &t) == 1) {
            wipes.back().off = t;
            continue;
        }
        if (!in_wipe || !is_move(line))
            continue;
        const std::string code = line.substr(0, line.find(';'));
        // The wipe runs at its own 10 mm/s, written once; the lift or the jump at its own speed ends it.
        const size_t f = code.find('F');
        if ((f != std::string::npos && std::stod(code.substr(f + 1)) != 600.) || code.find('Z') != std::string::npos ||
            code.find('E') != std::string::npos) {
            in_wipe = false;
            continue;
        }
        const size_t at = code.find('X');
        if (at != std::string::npos) {
            ++wipes.back().moves;
            const double x = std::stod(code.substr(at + 1));
            wipes.back().xmin = std::min(wipes.back().xmin, x);
            wipes.back().xmax = std::max(wipes.back().xmax, x);
        }
    }

    // One jump per layer after the first section is printed.
    REQUIRE(wipes.size() >= 29);
    for (const Wipe &w : wipes) {
        INFO("wipe " << w.seconds << " s, " << w.moves << " moves, " << w.air << " mm over air, " << w.left << " of " << w.was
                     << " mm, x " << w.xmin << " to " << w.xmax << ", restart after " << w.off << " s");
        CHECK(w.was > limit);
        CHECK(w.left <= limit + 0.01);
        CHECK(w.seconds > 0.5);
        CHECK(w.air > 20.);
        CHECK(w.moves > 1);
        // Back and forth inside the section, not over the gap.
        CHECK(w.xmax - w.xmin < section_size);
        CHECK(((w.xmax < 100. - section_gap / 2.) || (w.xmin > 100. + section_gap / 2.)));
        // The restart reprimes for the wipe as well as the jump.
        CHECK(w.off > w.seconds);
    }

    // Off by default.
    CHECK(count_of(slice_two_sections({}), "; OOZE wipe") == 0);
}

TEST_CASE("A restart onto ironing pushes no reprime or boost", "[OozeStop][OozeReprime]")
{
    const std::string gcode = slice_two_sections({{"top_shell_layers", 2}, {"ironing_type", "top"}, {"ooze_reprime_boost", 1}});
    REQUIRE(count_role_tags(gcode, "Ironing") > 0);

    // Every restart after a retraction: the role of the line it starts, and whether it reprimed.
    size_t             ironing_restarts = 0, ironing_reprimed = 0, other_reprimed = 0;
    bool               restarting = false, reprimed = false;
    std::string        role;
    std::istringstream in(gcode);
    std::string        line;
    while (std::getline(in, line)) {
        if (line.rfind("; FEATURE: ", 0) == 0)
            role = line.substr(11);
        else if (line.rfind(";TYPE:", 0) == 0)
            role = line.substr(6);
        else if (line.rfind("; OOZE reprime", 0) == 0)
            reprimed = true;
        else if (line.rfind("G1 E", 0) == 0 && std::stod(line.substr(4)) > 0.)
            restarting = true;
        else if (restarting && line.rfind("G1 X", 0) == 0 && line.find(" E") != std::string::npos) {
            if (role == "Ironing") {
                ++ironing_restarts;
                ironing_reprimed += reprimed ? 1 : 0;
            } else
                other_reprimed += reprimed ? 1 : 0;
            restarting = reprimed = false;
        }
    }
    INFO(after_first(gcode, ";TYPE:Ironing"));
    REQUIRE(ironing_restarts > 0);
    CHECK(ironing_reprimed == 0);
    // The jumps between the sections still reprime.
    CHECK(other_reprimed >= 29);
}
