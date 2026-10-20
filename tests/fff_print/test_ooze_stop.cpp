#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

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

std::string slice_ooze_cube(const std::string &name, DynamicPrintConfig config, bool bambu_x1)
{
    if (bambu_x1)
        config.set_deserialize_strict({{"printer_model", "Bambu Lab X1 Carbon"}});
    Print print;
    print.is_BBL_printer() = bambu_x1;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(4.)}, print, model, config);
    model.objects.front()->name = name;
    print.apply(model, config);
    return Test::gcode(print);
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
    const std::string gcode = slice_ooze_cube("ooze W5 R1 C10", ooze_config(), true);

    CHECK(count_of(gcode, "; OOZE stop") == ooze_layers);
    CHECK(count_of(gcode, "M400 S5") == ooze_layers);
    CHECK(gcode.find(" Y265") != std::string::npos);
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
