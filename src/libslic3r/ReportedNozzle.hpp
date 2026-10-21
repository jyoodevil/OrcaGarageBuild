#pragma once

namespace Slic3r {

class ConfigBase;
class DynamicPrintConfig;

// OrcaGarageBuild: printers such as Bambu Lab's refuse a sliced file whose nozzle size isn't one they can
// be set to. With report_variant_nozzle on, the nozzle size written into sliced files for the printer
// (the G-code config block, and the plate and project data of a .gcode.3mf) is the diameter of the
// printer preset's variant instead of the real one. Slicing itself keeps the real diameter.

// Diameter to report, or 0 to report the real one. Falls back to 0.4 mm when the variant isn't a number.
double nozzle_diameter_reported_to_printer(const ConfigBase &config);

// Sets every nozzle_diameter value of a config that is about to be written for the printer to the
// reported diameter. Leaves the config alone when report_variant_nozzle is off.
void apply_nozzle_diameter_reported_to_printer(DynamicPrintConfig &config);

} // namespace Slic3r
