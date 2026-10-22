#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r/GCode/OozeStop.hpp"
#include "libslic3r/GCode/OozeTower.hpp"

#include <cmath>

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

TEST_CASE("An ooze object name can ask for a tower, a deeper retraction and a settle", "[OozeStop]")
{
    const std::optional<OozeStopSpec> spec = parse_ooze_object_name("ooze W30 R0.5 C40 D2.5 S1.25 T");
    REQUIRE(spec.has_value());
    CHECK(spec->tower);
    CHECK_FALSE(spec->hover);
    CHECK_THAT(spec->extra_retract, WithinAbs(2.5, 1e-9));
    CHECK_THAT(spec->restart_dwell, WithinAbs(1.25, 1e-9));
    CHECK(spec->cool_deg() == 40);

    // Hovering stays over the part, so a tower token has nothing to do.
    const std::optional<OozeStopSpec> hover = parse_ooze_object_name("ooze W30 T H");
    REQUIRE(hover.has_value());
    CHECK(hover->hover);
    CHECK_FALSE(hover->tower);
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
    spec.extra_retract = 1.5;
    spec.restart_dwell = 0.5;
    spec.tower        = true;
    spec.purge        = true;
    const std::string name = ooze_object_name(spec);
    const std::optional<OozeStopSpec> back = parse_ooze_object_name(name);
    INFO(name);
    REQUIRE(back.has_value());
    CHECK_THAT(back->wait_s, WithinAbs(40., 1e-9));
    CHECK_THAT(back->reprime_at_layer(4), WithinAbs(1.25, 1e-9));
    CHECK(back->cool_deg() == 15);
    CHECK_THAT(back->extra_retract, WithinAbs(1.5, 1e-9));
    CHECK_THAT(back->restart_dwell, WithinAbs(0.5, 1e-9));
    CHECK(back->tower);
    CHECK(back->purge);
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
    params.stop          = OozeStopKind::Chute;
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
    params.stop        = OozeStopKind::Chute;
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
    params.stop          = OozeStopKind::Chute;
    params.reprime_count = 10;
    std::string error;
    CHECK_FALSE(ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(120., 120.), &error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("The ooze ladder leaves room for a tower behind every cylinder", "[OozeStop]")
{
    // A 1.8 mm nozzle at 0.7 mm layers with the default 25 mm, 3-wall tower and 2 brim loops.
    const OozeTowerShape shape = make_ooze_tower_shape(25., 3, 2, 1.8, 0.7);
    REQUIRE(shape.walls == 3);
    OozeCalibParams params; // towers, 4 waits by 6 reprime lengths
    params.tower_footprint = 2. * shape.footprint_radius();
    std::string error;
    const std::optional<OozeCalibLayout> layout = ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(256., 256.), &error);
    INFO(error);
    REQUIRE(layout.has_value());
    REQUIRE(layout->objects.size() == params.waits.size() * size_t(params.reprime_count));
    CHECK(layout->tower_dy >= 0.5 * layout->diameter + layout->brim + ooze_tower_clearance + shape.footprint_radius() - 1e-9);
    const double r = shape.footprint_radius();
    for (const OozeCalibObject &object : layout->objects) {
        INFO(object.name);
        const std::optional<OozeStopSpec> spec = parse_ooze_object_name(object.name);
        REQUIRE(spec.has_value());
        CHECK(spec->tower);
        CHECK_THAT(spec->extra_retract, WithinAbs(params.extra_retract, 1e-9));
        CHECK_THAT(spec->restart_dwell, WithinAbs(params.restart_dwell, 1e-9));
        const Vec2d tower = object.center + Vec2d(0., layout->tower_dy);
        CHECK(tower.x() - r >= 0.);
        CHECK(tower.x() + r <= 256.);
        CHECK(tower.y() + r <= 256.);
        // No tower reaches another cylinder or tower.
        for (const OozeCalibObject &other : layout->objects) {
            if (&other == &object)
                continue;
            const Vec2d other_tower = other.center + Vec2d(0., layout->tower_dy);
            CHECK((tower - other_tower).norm() >= 2. * r);
            CHECK(ooze_distance_to_box(tower, other.center - Vec2d::Constant(0.5 * layout->diameter + layout->brim),
                                       other.center + Vec2d::Constant(0.5 * layout->diameter + layout->brim)) >= r);
        }
    }
}

TEST_CASE("An ooze ladder with towers needs a tower size", "[OozeStop]")
{
    OozeCalibParams params; // towers, but no footprint
    std::string error;
    CHECK_FALSE(ooze_calib_layout(params, Vec2d(0., 0.), Vec2d(256., 256.), &error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("An ooze tower's walls are printed inside out, with a brim outside them on the first layer", "[OozeStop][OozeTower]")
{
    const OozeTowerShape shape = make_ooze_tower_shape(20., 3, 2, 0.4, 0.2);
    REQUIRE(shape.walls == 3);
    CHECK_THAT(shape.line_width, WithinAbs(0.42, 1e-9));
    const std::vector<double> first = shape.loop_radii(true);
    const std::vector<double> other = shape.loop_radii(false);
    REQUIRE(first.size() == 5);
    REQUIRE(other.size() == 3);
    for (size_t i = 1; i < first.size(); ++i)
        CHECK_THAT(first[i] - first[i - 1], WithinAbs(shape.spacing, 1e-9));
    CHECK_THAT(other.back() + 0.5 * shape.spacing, WithinAbs(10., 1e-9));
    CHECK_THAT(first.back() + 0.5 * shape.spacing, WithinAbs(shape.footprint_radius(), 1e-9));
    CHECK(other.front() - 0.5 * shape.spacing > 0.);
}

TEST_CASE("An ooze tower too small for its walls has no walls", "[OozeStop][OozeTower]")
{
    CHECK(make_ooze_tower_shape(8., 3, 0, 1.8, 0.7).walls == 0);
}

TEST_CASE("An ooze tower layer is taken loop by loop, and the next layer starts on top of it", "[OozeStop][OozeTower]")
{
    const OozeTowerShape shape = make_ooze_tower_shape(20., 3, 2, 0.4, 0.2);
    OozeTower            tower(Vec2d(50., 60.), shape);
    tower.start_layer(0.2);
    CHECK(tower.first_layer());
    CHECK_THAT(tower.layer_height(), WithinAbs(0.2, 1e-9));
    CHECK(tower.loops_left() == 5);
    CHECK(tower.take_loops(1).size() == 1);
    // Starting the same layer again changes nothing.
    tower.start_layer(0.2);
    CHECK(tower.loops_left() == 4);
    CHECK(tower.take_loops(10).size() == 4);
    CHECK(tower.loops_left() == 0);
    CHECK_THAT(tower.next_loop_radius(), WithinAbs(10. - 0.5 * shape.spacing, 1e-9));

    tower.start_layer(0.5);
    CHECK_FALSE(tower.first_layer());
    CHECK_THAT(tower.top_z(), WithinAbs(0.2, 1e-9));
    CHECK_THAT(tower.layer_height(), WithinAbs(0.3, 1e-9));
    CHECK(tower.loops_left() == 3);
}

TEST_CASE("An ooze tower loop closes on its start at the given radius", "[OozeStop][OozeTower]")
{
    const Vec2d              center(10., 20.);
    const std::vector<Vec2d> loop = ooze_tower_loop(center, 5., 0.5 * PI);
    REQUIRE(loop.size() >= 17);
    CHECK_THAT((loop.front() - loop.back()).norm(), WithinAbs(0., 1e-9));
    CHECK_THAT(loop.front().x(), WithinAbs(10., 1e-9));
    CHECK_THAT(loop.front().y(), WithinAbs(25., 1e-9));
    for (const Vec2d &p : loop)
        CHECK_THAT((p - center).norm(), WithinAbs(5., 1e-9));
}
