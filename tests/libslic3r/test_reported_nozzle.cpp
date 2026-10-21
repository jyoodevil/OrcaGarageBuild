#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ReportedNozzle.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {
DynamicPrintConfig nozzle_config(bool report, const std::string &variant)
{
    DynamicPrintConfig config;
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({1.8}));
    config.set_key_value("printer_variant", new ConfigOptionString(variant));
    config.set_key_value("report_variant_nozzle", new ConfigOptionBool(report));
    return config;
}
} // namespace

TEST_CASE("Sliced files report the real nozzle unless asked otherwise", "[ReportedNozzle]")
{
    DynamicPrintConfig config = nozzle_config(false, "0.4");
    CHECK_THAT(nozzle_diameter_reported_to_printer(config), WithinAbs(0., 1e-9));
    apply_nozzle_diameter_reported_to_printer(config);
    CHECK_THAT(config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0), WithinAbs(1.8, 1e-9));
}

TEST_CASE("Sliced files report the variant's nozzle when asked", "[ReportedNozzle]")
{
    DynamicPrintConfig config = nozzle_config(true, "0.6");
    CHECK_THAT(nozzle_diameter_reported_to_printer(config), WithinAbs(0.6, 1e-9));
    apply_nozzle_diameter_reported_to_printer(config);
    CHECK_THAT(config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0), WithinAbs(0.6, 1e-9));
}

TEST_CASE("A printer without a numeric variant reports a 0.4 mm nozzle", "[ReportedNozzle]")
{
    CHECK_THAT(nozzle_diameter_reported_to_printer(nozzle_config(true, "")), WithinAbs(0.4, 1e-9));
}
