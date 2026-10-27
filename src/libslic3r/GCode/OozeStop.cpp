#include "OozeStop.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <string>

#include "libslic3r/Exception.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/GCode/OozeTower.hpp"

namespace Slic3r {

static double parse_number(const std::string &s) { return std::strtod(s.c_str(), nullptr); }

// Shortest decimal form: 1.5, 0.25, 30.
static std::string compact_number(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}

double OozeStopSpec::reprime_at_layer(int object_layer) const
{
    if (step_layers <= 0 || object_layer <= 0)
        return reprime;
    return reprime + reprime_step * double(object_layer / step_layers);
}

std::optional<OozeStopSpec> parse_ooze_object_name(const std::string &name)
{
    using std::regex;
    static const regex keyword(R"(^\s*ooze(?:[\s_]|$))", regex::icase);
    if (!std::regex_search(name, keyword))
        return std::nullopt;

    static const regex wait_re(R"((?:^|[\s_])W(\d+(?:\.\d+)?))", regex::icase);
    static const regex reprime_re(R"((?:^|[\s_])R(-?\d+(?:\.\d+)?)(?:\+(\d+(?:\.\d+)?)/(\d+))?)", regex::icase);
    static const regex cool_re(R"((?:^|[\s_])C(\d+))", regex::icase);
    static const regex extra_retract_re(R"((?:^|[\s_])D(\d+(?:\.\d+)?))", regex::icase);
    static const regex restart_dwell_re(R"((?:^|[\s_])S(\d+(?:\.\d+)?))", regex::icase);
    static const regex tower_re(R"((?:^|[\s_])T(?=$|[\s_.]))", regex::icase);
    static const regex hover_re(R"((?:^|[\s_])H(?=$|[\s_.]))", regex::icase);
    static const regex purge_re(R"((?:^|[\s_])P(?=$|[\s_.]))", regex::icase);
    static const regex auto_re(R"((?:^|[\s_])A(?=$|[\s_.]))", regex::icase);
    static const regex boost_re(R"((?:^|[\s_])B(\d+(?:\.\d+)?))", regex::icase);

    std::smatch m;
    if (!std::regex_search(name, m, wait_re))
        throw Slic3r::SlicingError(
            format(_u8L("Object \"%1%\" is an ooze test object but has no wait time. Add W and the seconds to wait, "
                        "for example \"ooze W30 R1\"."),
                   name));

    OozeStopSpec spec;
    spec.wait_s = parse_number(m[1].str());
    if (std::regex_search(name, m, reprime_re)) {
        spec.reprime = parse_number(m[1].str());
        if (m[2].matched && m[3].matched) {
            spec.reprime_step = parse_number(m[2].str());
            spec.step_layers  = std::max(1, std::atoi(m[3].str().c_str()));
        }
    }
    if (std::regex_search(name, m, cool_re))
        spec.cool = std::atoi(m[1].str().c_str());
    if (std::regex_search(name, m, extra_retract_re))
        spec.extra_retract = parse_number(m[1].str());
    if (std::regex_search(name, m, restart_dwell_re))
        spec.restart_dwell = parse_number(m[1].str());
    spec.auto_reprime = std::regex_search(name, auto_re);
    if (std::regex_search(name, m, boost_re))
        spec.boost = parse_number(m[1].str());
    spec.hover = std::regex_search(name, hover_re);
    spec.tower = !spec.hover && std::regex_search(name, tower_re);
    spec.purge = !spec.hover && std::regex_search(name, purge_re);
    return spec;
}

std::string ooze_object_name(const OozeStopSpec &spec)
{
    std::string name = "ooze W" + compact_number(spec.wait_s);
    if (spec.auto_reprime)
        name += " A";
    else {
        name += " R" + compact_number(spec.reprime);
        if (spec.step_layers > 0 && spec.reprime_step != 0.)
            name += "+" + compact_number(spec.reprime_step) + "/" + std::to_string(spec.step_layers);
    }
    if (spec.boost > 0.)
        name += " B" + compact_number(spec.boost);
    if (spec.cool >= 0)
        name += " C" + std::to_string(spec.cool);
    if (spec.extra_retract > 0.)
        name += " D" + compact_number(spec.extra_retract);
    if (spec.restart_dwell > 0.)
        name += " S" + compact_number(spec.restart_dwell);
    if (spec.hover)
        return name + " H";
    if (spec.tower)
        name += " T";
    if (spec.purge)
        name += " P";
    return name;
}

bool printer_has_bambu_purge_chute(const std::string &printer_model)
{
    static const std::regex x1_p1(R"(\b(X1|P1)[A-Z]*\b)", std::regex::icase);
    return std::regex_search(printer_model, x1_p1);
}

std::string ooze_dwell_gcode(double seconds, bool bambu_firmware, const char *comment)
{
    const long total_ms = std::lround(std::max(0., seconds) * 1000.);
    if (total_ms <= 0)
        return {};
    std::string out = "M400\n";
    if (bambu_firmware) {
        const long whole = total_ms / 1000;
        const long ms    = total_ms % 1000;
        if (whole > 0)
            out += "M400 S" + std::to_string(whole) + " ; " + comment + "\n";
        if (ms > 0)
            out += "M400 P" + std::to_string(ms) + "\n";
    } else {
        out += "G4 P" + std::to_string(total_ms) + " ; " + comment + "\n";
    }
    return out;
}

double ooze_curve_reprime(double max_mm, double half_life_s, double seconds)
{
    if (!(max_mm > 0.) || !(seconds > 0.))
        return 0.;
    if (!(half_life_s > 0.))
        return max_mm;
    return max_mm * (1. - std::exp2(-seconds / half_life_s));
}

double ooze_boost_share(double from_mm, double to_mm, double distance_mm)
{
    if (!(to_mm > from_mm))
        return 0.;
    if (!(distance_mm > 0.))
        return from_mm <= 0. && to_mm > 0. ? 1. : 0.;
    // Share pushed by x mm along the line: the area under a ramp from 2/distance down to 0.
    const auto pushed = [distance_mm](double x) {
        const double left = 1. - std::clamp(x / distance_mm, 0., 1.);
        return 1. - left * left;
    };
    return pushed(to_mm) - pushed(from_mm);
}

double ooze_move_time(double distance, double speed, double accel)
{
    if (!(distance > 0.) || !(speed > 0.))
        return 0.;
    if (!(accel > 0.))
        return distance / speed;
    // Accelerating to full speed and braking from it takes speed^2 / accel of the move.
    if (distance >= speed * speed / accel)
        return distance / speed + speed / accel;
    return 2. * std::sqrt(distance / accel);
}

double ooze_gcode_time(const std::string &gcode, OozeGCodeClock clock)
{
    double seconds = 0.;
    size_t begin   = 0;
    while (begin < gcode.size()) {
        size_t end = gcode.find('\n', begin);
        if (end == std::string::npos)
            end = gcode.size();
        std::string line = gcode.substr(begin, end - begin);
        begin            = end + 1;
        if (const size_t comment = line.find(';'); comment != std::string::npos)
            line.resize(comment);

        // The command, then single-letter words such as X12.5, or KEY=value words.
        std::istringstream words(line);
        std::string        command;
        if (!(words >> command))
            continue;
        double      value[26] = {};
        bool        has[26]   = {};
        double      accel_word = 0.;
        std::string word;
        while (words >> word) {
            if (word.rfind("ACCEL=", 0) == 0)
                accel_word = std::strtod(word.c_str() + 6, nullptr);
            else if (word.size() >= 2 && std::isalpha(static_cast<unsigned char>(word[0]))) {
                const int letter = std::toupper(static_cast<unsigned char>(word[0])) - 'A';
                value[letter]    = std::strtod(word.c_str() + 1, nullptr);
                has[letter]      = true;
            }
        }
        const auto get = [&](char letter, double &out) {
            if (!has[letter - 'A'])
                return false;
            out = value[letter - 'A'];
            return true;
        };

        if (command == "G0" || command == "G1") {
            Vec3d  to = clock.position;
            double v  = 0.;
            if (get('X', v)) to.x() = v;
            if (get('Y', v)) to.y() = v;
            if (get('Z', v)) to.z() = v;
            if (get('F', v)) clock.feedrate = v;
            double de = 0.;
            if (get('E', v)) {
                de = clock.relative_e ? v : v - clock.e;
                if (!clock.relative_e)
                    clock.e = v;
            }
            const double speed    = clock.feedrate / 60.;
            const double distance = (to - clock.position).norm();
            if (distance > EPSILON) {
                // Z moves are slow and short; they run at their feedrate.
                const bool   xy    = (to.head<2>() - clock.position.head<2>()).norm() > EPSILON;
                const double accel = !xy ? 0. : de > 0. || !(clock.travel_accel > 0.) ? clock.print_accel : clock.travel_accel;
                seconds += ooze_move_time(distance, speed, accel);
            } else if (std::abs(de) > 0. && speed > 0.)
                seconds += std::abs(de) / speed;
            clock.position = to;
        } else if (command == "G4" || command == "M400") {
            double v = 0.;
            if (get('S', v))
                seconds += std::max(0., v);
            if (get('P', v))
                seconds += std::max(0., v) / 1000.;
        } else if (command == "M204") {
            double v = 0.;
            if (get('S', v))
                clock.print_accel = clock.travel_accel = v;
            if (get('P', v))
                clock.print_accel = v;
            if (get('T', v))
                clock.travel_accel = v;
        } else if (command == "SET_VELOCITY_LIMIT" && accel_word > 0.)
            clock.print_accel = clock.travel_accel = accel_word;
    }
    return seconds;
}


std::optional<OozeCalibLayout> ooze_calib_layout(const OozeCalibParams &p, const Vec2d &bed_min, const Vec2d &bed_max,
                                                 std::string *error)
{
    const auto fail = [error](const std::string &msg) -> std::optional<OozeCalibLayout> {
        if (error)
            *error = msg;
        return std::nullopt;
    };

    // Rows of waits, columns of reprime lengths.
    std::vector<double> waits;
    // Ladder: reprime rates per column; reprime check: reprime lengths; boost ladder: start boosts.
    std::vector<double> reprimes;
    OozeCalibLayout     layout;
    double              max_diameter = 0.;
    double              min_diameter = 0.;
    if (p.test == OozeCalibTest::Ladder || p.test == OozeCalibTest::Boost) {
        const bool boost = p.test == OozeCalibTest::Boost;
        waits            = boost ? p.boost_waits : p.waits;
        if (waits.empty() || waits.size() > 6)
            return fail(_u8L("Use 1 to 6 wait times."));
        if (boost) {
            if (p.boost_count < 1 || p.boost_count > 10)
                return fail(_u8L("Use 1 to 10 start boosts."));
            for (int i = 0; i < p.boost_count; ++i)
                reprimes.push_back(p.boost_start + i * p.boost_step);
            if (*std::min_element(reprimes.begin(), reprimes.end()) < 0.)
                return fail(_u8L("Start boosts cannot be negative."));
            if (!(p.ooze_max >= 0.) || !(p.half_life > 0.))
                return fail(_u8L("Enter the nozzle's maximum ooze and an ooze half-life above 0 seconds."));
            if (!(p.boost_distance > 0.))
                return fail(_u8L("The boost distance must be above 0 mm."));
        } else {
            if (p.reprime_count < 1 || p.reprime_count > 10)
                return fail(_u8L("Use 1 to 10 reprime rates."));
            for (int i = 0; i < p.reprime_count; ++i)
                reprimes.push_back(p.rate_start + i * p.rate_step);
        }
        layout.layers = boost ? p.boost_layers : p.ladder_layers;
        layout.brim   = 3.;
        // As large as the grid allows: a big nozzle makes a small cylinder hard to read.
        max_diameter  = 25.;
        min_diameter  = 10.;
    } else {
        waits = {p.wait};
        if (p.boxes < 1 || p.boxes > 8)
            return fail(_u8L("Use 1 to 8 cylinders."));
        for (int i = 0; i < p.boxes; ++i)
            reprimes.push_back(p.reprime_mid + (i - 0.5 * (p.boxes - 1)) * p.check_step);
        layout.layers = p.check_layers;
        layout.brim   = 5.;
        max_diameter  = 25.;
        min_diameter  = 15.;
    }
    for (double w : waits)
        if (!(w > 0.) || w > 600.)
            return fail(_u8L("Wait times must be between 0 and 600 seconds."));
    if (layout.layers < 1 || layout.layers > 50)
        return fail(_u8L("Use 1 to 50 layers."));
    if (p.extra_retract < 0. || p.extra_retract > 10.)
        return fail(_u8L("Retract further by 0 to 10 mm while waiting."));
    if (p.restart_dwell < 0. || p.restart_dwell > 30.)
        return fail(_u8L("Let the pressure build for 0 to 30 seconds after the reprime."));
    const bool tower = p.stop == OozeStopKind::Tower;
    if (tower && !(p.tower_footprint > 0.))
        return fail(_u8L("The ooze tower is too small for its walls. Make it wider or give it fewer walls."));

    // Each cell holds a cylinder with its brim and, behind it, its tower. Fit the grid inside the bed,
    // shrinking the cylinders down to a minimum diameter.
    const double margin   = 15.;
    const double gap      = 6.;
    const double footprint = tower ? p.tower_footprint : 0.;
    const double behind   = tower ? ooze_tower_clearance + footprint : 0.;
    const size_t cols     = reprimes.size();
    const size_t rows     = waits.size();
    const Vec2d  room     = bed_max - bed_min - Vec2d(2. * margin, 2. * margin);
    // Width and depth of a cell for a cylinder diameter.
    const auto   cell_w   = [&](double d) { return std::max(d + 2. * layout.brim, footprint); };
    const auto   cell_d   = [&](double d) { return d + 2. * layout.brim + behind; };
    const double fit_y    = (room.y() + gap) / double(rows) - gap - 2. * layout.brim - behind;
    double       fit_x    = (room.x() + gap) / double(cols) - gap - 2. * layout.brim;
    if (cols * (footprint + gap) - gap > room.x() + EPSILON)
        fit_x = 0.;
    layout.diameter = std::min({max_diameter, fit_x, fit_y});
    if (layout.diameter < min_diameter)
        return fail(tower ? _u8L("The test and its ooze towers do not fit on the bed. Use fewer wait times or "
                                 "cylinders, or a smaller ooze tower.") :
                            _u8L("The test does not fit on the bed. Use fewer wait times or cylinders."));
    if (tower)
        layout.tower_dy = 0.5 * layout.diameter + layout.brim + ooze_tower_clearance + 0.5 * footprint;

    const Vec2d pitch(cell_w(layout.diameter) + gap, cell_d(layout.diameter) + gap);
    const Vec2d grid_min = 0.5 * (bed_min + bed_max) - 0.5 * Vec2d(cols * pitch.x() - gap, rows * pitch.y() - gap);
    for (size_t row = 0; row < rows; ++row)
        for (size_t col = 0; col < cols; ++col) {
            OozeStopSpec spec;
            spec.wait_s        = waits[row];
            switch (p.test) {
            case OozeCalibTest::Ladder: spec.reprime = p.reprime_base + reprimes[col] * waits[row]; break;
            case OozeCalibTest::Check: spec.reprime = reprimes[col]; break;
            case OozeCalibTest::Boost:
                spec.auto_reprime = true;
                spec.boost        = reprimes[col];
                break;
            }
            spec.hover         = p.stop == OozeStopKind::Hover;
            spec.tower         = tower;
            spec.purge         = p.test == OozeCalibTest::Check && !spec.hover && p.purge;
            spec.cool          = spec.hover ? -1 : std::max(0, p.cool);
            spec.extra_retract = p.extra_retract;
            spec.restart_dwell = p.restart_dwell;
            // Rows run front to back, columns left to right; the cylinder sits at the front of its cell.
            const Vec2d cell_min = grid_min + Vec2d(col * pitch.x(), row * pitch.y());
            const Vec2d center   = cell_min + Vec2d(0.5 * cell_w(layout.diameter), layout.brim + 0.5 * layout.diameter);
            layout.objects.push_back({ooze_object_name(spec), center});
            layout.stops += layout.layers;
            layout.wait_total += waits[row] * layout.layers;
        }
    return layout;
}

} // namespace Slic3r
