#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"

#include <string>

using namespace Slic3r;

TEST_CASE("The G-code config block carries the nozzle reported to the printer", "[ReportedNozzle]")
{
    // A 0.8 mm nozzle on a printer whose variant is 0.4 mm.
    const auto [report, written] = GENERATE(table<std::string, std::string>({{"0", "0.8"}, {"1", "0.4"}}));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"nozzle_diameter", "0.8"}, {"printer_variant", "0.4"}, {"report_variant_nozzle", report}});
    const std::string gcode = Test::slice({Test::cube(20)}, config);
    INFO("report_variant_nozzle " << report);
    CHECK(gcode.find("; nozzle_diameter = " + written + "\n") != std::string::npos);
}
