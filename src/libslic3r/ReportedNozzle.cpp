#include "ReportedNozzle.hpp"

#include "Config.hpp"
#include "LocalesUtils.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

double nozzle_diameter_reported_to_printer(const ConfigBase &config)
{
    const auto *enabled = config.option<ConfigOptionBool>("report_variant_nozzle");
    if (enabled == nullptr || !enabled->value)
        return 0.;
    double diameter = 0.;
    if (const auto *variant = config.option<ConfigOptionString>("printer_variant"); variant != nullptr && !variant->value.empty()) {
        try {
            diameter = string_to_double_decimal_point(variant->value);
        } catch (...) {
            diameter = 0.;
        }
    }
    return diameter > 0. ? diameter : 0.4;
}

void apply_nozzle_diameter_reported_to_printer(DynamicPrintConfig &config)
{
    const double diameter = nozzle_diameter_reported_to_printer(config);
    if (diameter <= 0.)
        return;
    if (auto *nozzle_diameter = config.option<ConfigOptionFloats>("nozzle_diameter"))
        for (double &value : nozzle_diameter->values)
            value = diameter;
}

} // namespace Slic3r
