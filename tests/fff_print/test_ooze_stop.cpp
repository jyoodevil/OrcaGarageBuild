#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/OozeTower.hpp"

#include "test_helpers.hpp"

#include <cstddef>
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

std::string slice_ooze_cube(const std::string &name, DynamicPrintConfig config, bool bambu_x1)
{
    if (bambu_x1)
        config.set_deserialize_strict({{"printer_model", "Bambu Lab X1 Carbon"}});
    Print print;
    print.is_BBL_printer() = bambu_x1;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(4.)}, print, model, config);
    ModelObject *object = model.objects.front();
    object->name        = name;
    object->center_around_origin();
    object->instances.front()->set_offset(Vec3d(ooze_cube_center.x(), ooze_cube_center.y(), object->instances.front()->get_offset().z()));
    object->ensure_on_bed();
    print.apply(model, config);
    return Test::gcode(print);
}

// Positive E-only moves (pushes back after a retraction) after each "; OOZE stop" marker, up to count per stop.
std::vector<std::vector<double>> pushes_after(const std::string &gcode, const std::string &marker, size_t count)
{
    std::vector<std::vector<double>> out;
    std::istringstream               in(gcode);
    std::string                      line;
    while (std::getline(in, line)) {
        if (line.rfind(marker, 0) == 0) {
            out.emplace_back();
            continue;
        }
        if (out.empty() || out.back().size() >= count || line.rfind("G1 E", 0) != 0)
            continue;
        const double e = std::stod(line.substr(4));
        if (e > 0.)
            out.back().push_back(e);
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

// E of the first positive E-only move after each "; OOZE stop" marker: the restart after the stop.
std::vector<double> restart_lengths(const std::string &gcode)
{
    std::vector<double> out;
    std::istringstream  in(gcode);
    std::string         line;
    bool                after_stop = false;
    while (std::getline(in, line)) {
        if (line.rfind("; OOZE stop", 0) == 0) {
            after_stop = true;
            continue;
        }
        if (!after_stop || line.rfind("G1 E", 0) != 0)
            continue;
        const double e = std::stod(line.substr(4));
        if (e > 0.) {
            out.push_back(e);
            after_stop = false;
        }
    }
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

TEST_CASE("A tower stop waits over the hollow of a tower behind the object and prints the tower before restarting",
          "[OozeStop][OozeTower]")
{
    // No purge chute needed. Retract 2 mm more while waiting, then reprime 1 mm.
    const std::string gcode = slice_ooze_cube("ooze W5 R1 D2 T", ooze_config(), false);

    CHECK(count_of(gcode, "; OOZE stop") == ooze_layers);
    CHECK(count_of(gcode, "G4 P5000") == ooze_layers);
    // One layer of the tower per stop.
    CHECK(count_of(gcode, "; FEATURE: Prime tower") == ooze_layers);

    // The nozzle waits behind the cube, clear of it.
    std::istringstream in(gcode);
    std::string        line;
    double             x = 0., y = 0.;
    std::vector<Vec2d> waits;
    while (std::getline(in, line)) {
        if (line.rfind("G4 P5000", 0) == 0)
            waits.emplace_back(x, y);
        if (line.rfind("G1 ", 0) != 0)
            continue;
        if (const size_t at = line.find(" X"); at != std::string::npos)
            x = std::stod(line.substr(at + 2));
        if (const size_t at = line.find(" Y"); at != std::string::npos)
            y = std::stod(line.substr(at + 2));
    }
    REQUIRE(waits.size() == ooze_layers);
    for (const Vec2d &wait : waits) {
        CHECK_THAT(wait.x(), WithinAbs(ooze_cube_center.x(), 1e-3));
        CHECK(wait.y() > ooze_cube_center.y() + 2. + ooze_tower_clearance);
    }

    // The tower takes back the retraction and the extra 2 mm; the cube restarts with the retraction and the reprime.
    const std::vector<std::vector<double>> pushes = pushes_after(gcode, "; OOZE stop", 2);
    REQUIRE(pushes.size() == ooze_layers);
    for (const std::vector<double> &stop : pushes) {
        REQUIRE(stop.size() == 2);
        CHECK_THAT(stop[0], WithinAbs(ooze_retraction + 2., 1e-4));
        CHECK_THAT(stop[1], WithinAbs(ooze_retraction + 1., 1e-4));
    }
}

TEST_CASE("An ooze tower that does not fit on the bed is an error", "[OozeStop][OozeTower]")
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({{"ooze_tower_diameter", 300}});
    CHECK_THROWS(slice_ooze_cube("ooze W5 R1 T", config, false));
}

namespace {

// Two 4 mm cubes 140 mm apart, with the print's ooze tower between and behind them.
std::string slice_two_cubes_with_ooze_tower(bool tower)
{
    DynamicPrintConfig config = ooze_config();
    config.set_deserialize_strict({
        {"travel_speed", 50},
        {"ooze_tower", tower ? 1 : 0},
        {"ooze_tower_x", 100},
        {"ooze_tower_y", 140},
        // A trip between the cubes takes 2.8 s, from the tower to either cube under 1.4 s.
        {"ooze_tower_travel_time", 2},
        {"ooze_tower_reprime", 1.5},
    });
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(4.), Test::cube(4.)}, print, model, config);
    const double x[] = {30., 170.};
    for (size_t i = 0; i < 2; ++i) {
        model.objects[i]->center_around_origin();
        ModelInstance *instance = model.objects[i]->instances.front();
        instance->set_offset(Vec3d(x[i], 100., instance->get_offset().z()));
        model.objects[i]->ensure_on_bed();
    }
    print.apply(model, config);
    return Test::gcode(print);
}

} // namespace

TEST_CASE("Long travels between parts go through the print's ooze tower, which grows every layer", "[OozeStop][OozeTower]")
{
    const std::string gcode = slice_two_cubes_with_ooze_tower(true);

    // Each layer travels once between the cubes, and the rest of the tower's layer is printed at its end.
    CHECK(count_of(gcode, "; OOZE tower: visit") >= ooze_layers);
    CHECK(count_of(gcode, "; OOZE tower: rest of the layer") == ooze_layers);

    // After the tower, the part restarts with the retraction and the tower's reprime.
    const std::vector<std::vector<double>> pushes = pushes_after(gcode, "; OOZE tower: visit", 2);
    REQUIRE_FALSE(pushes.empty());
    for (const std::vector<double> &visit : pushes) {
        REQUIRE(visit.size() == 2);
        CHECK_THAT(visit[1], WithinAbs(ooze_retraction + 1.5, 1e-4));
    }
}

TEST_CASE("Without the ooze tower option, travels go straight", "[OozeStop][OozeTower]")
{
    const std::string gcode = slice_two_cubes_with_ooze_tower(false);
    CHECK(gcode.find("; OOZE tower") == std::string::npos);
    CHECK(gcode.find("; FEATURE: Prime tower") == std::string::npos);
}
