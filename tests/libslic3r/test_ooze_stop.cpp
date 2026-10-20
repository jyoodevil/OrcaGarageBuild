#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r/GCode/OozeStop.hpp"

#include <string>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

TEST_CASE("An ooze object name gives the wait, the reprime and the stop type", "[OozeStop]")
{
    const std::optional<OozeStopSpec> spec = parse_ooze_object_name("ooze W30 R1.2");
    REQUIRE(spec.has_value());
    CHECK_THAT(spec->wait_s, WithinAbs(30., 1e-9));
    CHECK_THAT(spec->reprime_at_layer(0), WithinAbs(1.2, 1e-9));
    CHECK_FALSE(spec->hover);
    CHECK_FALSE(spec->purge);
    // Parking cools by 20 degrees unless the name says otherwise.
    CHECK(spec->cool_deg() == 20);
}

TEST_CASE("An ooze reprime ramp rises by its step every given number of layers", "[OozeStop]")
{
    const std::optional<OozeStopSpec> spec = parse_ooze_object_name("ooze W20 R0+0.5/3");
    REQUIRE(spec.has_value());
    CHECK_THAT(spec->reprime_at_layer(0), WithinAbs(0., 1e-9));
    CHECK_THAT(spec->reprime_at_layer(2), WithinAbs(0., 1e-9));
    CHECK_THAT(spec->reprime_at_layer(3), WithinAbs(0.5, 1e-9));
    CHECK_THAT(spec->reprime_at_layer(7), WithinAbs(1., 1e-9));
}

TEST_CASE("Ooze name tokens ignore case, take underscores and a file extension", "[OozeStop]")
{
    const std::optional<OozeStopSpec> hover = parse_ooze_object_name("OOZE w60 r1 c5 h.stl");
    REQUIRE(hover.has_value());
    CHECK_THAT(hover->wait_s, WithinAbs(60., 1e-9));
    CHECK(hover->hover);
    CHECK(hover->cool_deg() == 5);

    const std::optional<OozeStopSpec> purge = parse_ooze_object_name("ooze_W5_R-0.25_P");
    REQUIRE(purge.has_value());
    CHECK(purge->purge);
    CHECK_THAT(purge->reprime_at_layer(0), WithinAbs(-0.25, 1e-9));
}

TEST_CASE("Hovering does not cool the nozzle unless asked", "[OozeStop]")
{
    const std::optional<OozeStopSpec> spec = parse_ooze_object_name("ooze W10 R1 H");
    REQUIRE(spec.has_value());
    CHECK(spec->cool_deg() == 0);
}

TEST_CASE("Names that are not ooze objects are left alone", "[OozeStop]")
{
    CHECK_FALSE(parse_ooze_object_name("Cube").has_value());
    CHECK_FALSE(parse_ooze_object_name("oozebox.stl").has_value());
}

TEST_CASE("An ooze object without a wait time is an error", "[OozeStop]")
{
    CHECK_THROWS_AS(parse_ooze_object_name("ooze test"), Slic3r::SlicingError);
}

TEST_CASE("An ooze object name reads back as the same stop", "[OozeStop]")
{
    OozeStopSpec spec;
    spec.wait_s       = 40.;
    spec.reprime      = 0.75;
    spec.reprime_step = 0.25;
    spec.step_layers  = 2;
    spec.cool         = 15;
    const std::string name = ooze_object_name(spec);
    const std::optional<OozeStopSpec> back = parse_ooze_object_name(name);
    INFO(name);
    REQUIRE(back.has_value());
    CHECK_THAT(back->wait_s, WithinAbs(40., 1e-9));
    CHECK_THAT(back->reprime_at_layer(4), WithinAbs(1.25, 1e-9));
    CHECK(back->cool_deg() == 15);
}

TEST_CASE("Only Bambu Lab X1 and P1 printers park over a purge chute", "[OozeStop]")
{
    CHECK(printer_has_bambu_purge_chute("Bambu Lab X1 Carbon"));
    CHECK(printer_has_bambu_purge_chute("Bambu Lab X1E"));
    CHECK(printer_has_bambu_purge_chute("Bambu Lab P1S"));
    CHECK_FALSE(printer_has_bambu_purge_chute("Bambu Lab A1"));
    CHECK_FALSE(printer_has_bambu_purge_chute("Sovol SV06"));
}

TEST_CASE("Ooze waits use M400 on Bambu firmware and G4 elsewhere", "[OozeStop]")
{
    CHECK(ooze_dwell_gcode(30., true).find("M400 S30") != std::string::npos);
    CHECK(ooze_dwell_gcode(2.5, false).find("G4 P2500") != std::string::npos);
    CHECK(ooze_dwell_gcode(0., true).empty());
}

TEST_CASE("The ooze ladder puts one row per wait and one column per reprime on the bed", "[OozeStop]")
{
    OozeCalibParams params;
    params.waits         = {5., 20., 80.};
    params.reprime_start = 0.;
    params.reprime_step  = 0.5;
    params.reprime_count = 4;
    params.ladder_layers = 2;
    std::string error;
    const std::optional<OozeCalibLayout> layout = ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(256., 256.), &error);
    INFO(error);
    REQUIRE(layout.has_value());
    REQUIRE(layout->objects.size() == 12);
    CHECK(layout->stops == 12 * 2);
    CHECK_THAT(layout->wait_total, WithinAbs((5. + 20. + 80.) * 4 * 2, 1e-9));
    for (const OozeCalibObject &object : layout->objects) {
        INFO(object.name);
        CHECK(parse_ooze_object_name(object.name).has_value());
        CHECK(object.center.x() - layout->diameter / 2. - layout->brim >= 0.);
        CHECK(object.center.x() + layout->diameter / 2. + layout->brim <= 256.);
        CHECK(object.center.y() - layout->diameter / 2. - layout->brim >= 0.);
        CHECK(object.center.y() + layout->diameter / 2. + layout->brim <= 256.);
    }
    // Front row waits 5 s, last column reprimes 1.5 mm.
    const std::optional<OozeStopSpec> front_right = parse_ooze_object_name(layout->objects[3].name);
    REQUIRE(front_right.has_value());
    CHECK_THAT(front_right->wait_s, WithinAbs(5., 1e-9));
    CHECK_THAT(front_right->reprime, WithinAbs(1.5, 1e-9));
    CHECK(layout->objects[3].center.x() > layout->objects[0].center.x());
    CHECK(layout->objects[4].center.y() > layout->objects[0].center.y());
}

TEST_CASE("The reprime check centres its reprime lengths on the chosen value", "[OozeStop]")
{
    OozeCalibParams params;
    params.ladder      = false;
    params.wait        = 30.;
    params.reprime_mid = 1.;
    params.check_step  = 0.25;
    params.boxes       = 5;
    params.purge       = true;
    std::string error;
    const std::optional<OozeCalibLayout> layout = ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(256., 256.), &error);
    REQUIRE(layout.has_value());
    REQUIRE(layout->objects.size() == 5);
    const std::optional<OozeStopSpec> first = parse_ooze_object_name(layout->objects.front().name);
    const std::optional<OozeStopSpec> last  = parse_ooze_object_name(layout->objects.back().name);
    REQUIRE(first.has_value());
    REQUIRE(last.has_value());
    CHECK_THAT(first->reprime, WithinAbs(0.5, 1e-9));
    CHECK_THAT(last->reprime, WithinAbs(1.5, 1e-9));
    CHECK(first->purge);
}

TEST_CASE("An ooze ladder too wide for the bed is refused", "[OozeStop]")
{
    OozeCalibParams params;
    params.reprime_count = 10;
    std::string error;
    CHECK_FALSE(ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(120., 120.), &error).has_value());
    CHECK_FALSE(error.empty());
}
