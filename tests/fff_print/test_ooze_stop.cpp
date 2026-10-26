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

// Two 4 mm cubes 140 mm apart at 50 mm/s travel, so a trip between them takes about 2.8 s, optionally with the print's
// ooze tower between and behind them and with reprime by travel time.
std::string slice_two_cubes(bool tower, bool reprime)
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({
        {"travel_speed", 50},
        {"ooze_tower", tower ? 1 : 0},
        {"ooze_tower_x", 100},
        {"ooze_tower_y", 140},
        {"ooze_tower_travel_time", 2},
        // Reprime 1 mm plus 0.5 mm per second off the print, for at most 2 s.
        {"ooze_reprime", reprime ? 1 : 0},
        {"ooze_reprime_base", 1},
        {"ooze_reprime_amount", 0.5},
        {"ooze_reprime_interval", 1},
        {"ooze_reprime_time_limit", 2},
        {"ooze_reprime_min_time", 1},
        {"ooze_reprime_settle", 0.5},
    });
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
std::vector<ReprimeNote> reprime_notes(const std::string &gcode)
{
    std::vector<ReprimeNote> out;
    std::istringstream       in(gcode);
    std::string              line;
    bool                     want_push = false;
    while (std::getline(in, line)) {
        double t = 0., r = 0.;
        if (std::sscanf(line.c_str(), "; OOZE reprime: %lf s off the print, reprime %lf mm", &t, &r) == 2) {
            out.push_back({t, r});
            want_push = true;
        } else if (want_push && line.rfind("G1 E", 0) == 0) {
            out.back().pushed = std::stod(line.substr(4));
            want_push         = false;
        }
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
    // Each visit lets the nozzle drain over the tower for the ooze time limit.
    CHECK(count_of(gcode, "G4 P2000") == count_of(gcode, "; OOZE tower: visit"));

    // After the tower the part restarts with the retraction and the reprime for a drained nozzle: 1 + 0.5 * 2 mm,
    // even though reprime by travel time is off. Then it settles.
    const std::vector<std::vector<double>> pushes = push_groups(gcode, "; OOZE tower: visit", 2);
    REQUIRE_FALSE(pushes.empty());
    for (const std::vector<double> &visit : pushes) {
        REQUIRE(visit.size() == 2);
        CHECK_THAT(visit[1], WithinAbs(ooze_retraction + 2., 1e-4));
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

TEST_CASE("A restart after a long travel reprimes by the travel's time", "[OozeStop][OozeReprime]")
{
    const std::string              gcode = slice_two_cubes(false, true);
    const std::vector<ReprimeNote> notes = reprime_notes(gcode);

    // Every travel between the cubes takes more than 2 s; shorter travels inside a cube restart normally.
    size_t long_travels = 0;
    for (const ReprimeNote &note : notes) {
        INFO("travel " << note.time << " s");
        CHECK(note.time >= 1.);
        CHECK_THAT(note.reprime, WithinAbs(1. + 0.5 * std::min(note.time, 2.), 1e-3));
        CHECK_THAT(note.pushed, WithinAbs(note.reprime, 1e-3));
        if (note.time > 2.)
            ++long_travels;
    }
    CHECK(long_travels >= size_t(ooze_layers - 1));
    // Each reprime settles before the nozzle moves.
    CHECK(count_of(gcode, "G4 P500") == notes.size());
}
