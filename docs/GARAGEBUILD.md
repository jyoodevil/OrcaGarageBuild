# OrcaGarageBuild

OrcaGarageBuild is a personal test fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer). It is upstream
OrcaSlicer with these additions:

- **Wave overhangs** from [OrcaSlicer-WaveOverhangs](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs)
  (settings: [WAVE_OVERHANG_SETTINGS.md](WAVE_OVERHANG_SETTINGS.md)).
- **Ooze and reprime calibration**, for large nozzles that lose pressure while they wait.
- **Ooze control** for normal prints: reprime by the time off the print, a start boost, and an ooze tower that long
  travels go through.
- **Report the variant's nozzle to the printer**, for nozzle sizes a Bambu Lab printer can't be set to.

It installs as *OrcaGarageBuild* (`orca-garagebuild.exe`). It keeps its settings in its own folder
(`%APPDATA%\OrcaGarageBuild` on Windows), so it never touches an existing OrcaSlicer install. On first launch it
offers to copy profiles from OrcaSlicer WaveOverhangs, OrcaSlicer, Bambu Studio or PrusaSlicer. The same import is
under **File > Import > Import from other slicer**.

## Ooze and reprime calibration

A big nozzle keeps oozing after a retraction. What drips out while it waits ends up on the part when it comes back,
and the restart is short of material by however much it lost. This calibration measures both, so the restart can
push the right amount back in and a print knows when to clean the nozzle on an ooze tower first.

Open **Calibration > Ooze and reprime**. There are three tests:

- **Ooze ladder**: a grid of short hollow cylinders. Rows run front to back by wait time, and columns run left to
  right by reprime rate. Before every layer of every cylinder, the nozzle stops for that row's wait, then restarts
  with a reprime of the **base reprime** plus the column's rate times the row's wait, so each column tests one ooze
  rate at every wait (the cylinder's name shows its total). Look at the seam on the back of each cylinder: a gap
  means too little reprime and a blob means too much.
- **Reprime check**: one row of taller cylinders that all wait the same time, with reprime lengths around one
  value. Use it to confirm a value from the ladder over many layers. It can also purge the reprime into the tower or
  chute and restart normally, which tests priming there instead of at the part.
- **Boost ladder**: rows by wait time again, but every cylinder reprimes what the ooze curve (below) gives for its
  real time off the cylinder, and the columns rise by **start boost**: extra filament pushed along the start of the
  line as the nozzle moves off. Use it once the curve is known, to fill the start of the line without a blob.

Reprime lengths are millimetres of filament pushed back in after the stop. They replace the *extra length on
restart* (`retract_restart_extra`) for that restart only.

### Stops

- **Over an ooze tower** (any printer, the default): each cylinder gets its own ooze tower right behind it. Every
  stop is one ooze cycle, in the order a print meets it:
  1. Print the tower's layer, from the inside out. This leaves the nozzle clean and primed, so every wait starts
     from the same state.
  2. Retract, lift 2 mm and wait over the tower's hollow centre for the row's wait time; what drips falls inside.
  3. Come down to the top of the tower and wipe across its walls towards the cylinder, catching what hangs from the
     nozzle.
  4. Go back down onto the cylinder's restart point, push the retraction back, then push the reprime there at half
     the filament's maximum volumetric speed, so a reprime of many millimetres melts as it goes in instead of
     grinding the filament. Then wait for the **settle** time so the pressure builds before the nozzle moves.

  The towers show up when the plate is sliced; their size comes from the print settings under
  **Others > Ooze tower**.
- **Park** (Bambu Lab X1 and P1): the nozzle parks over the purge chute, cools while it waits, then shakes off and
  wipes on the silicone wiper the way Bambu's filament change does. This needs the wiper in place.
- **Hover** (any printer): the nozzle lifts 1 mm or one layer height, whichever is more, and waits above the restart
  point, as it would during a long travel. Use it with short waits to find how long a travel can be before the
  restart suffers; that is the ooze tower's **Travel time** for normal prints.

Every stop can also cool while it waits and retract further while it waits (**Retract further while waiting**),
pushing it back on restart. Both are off by default, so a wait behaves like a travel. A deeper retraction oozes less,
but too deep can pull molten filament into the cold zone.

The ladder's defaults are made for a first look: waits of 5, 15, 30 and 60 seconds, a 3 mm base reprime and reprime
rates from 0.1 to 0.6 mm per second of wait, with no settle. A 60 second wait then reprimes from 9 to 39 mm.

Each stop also takes a few seconds to go over the tower and back, so the nozzle is off the cylinder for longer than
the wait. Every restart notes the real time in the G-code, for example
`; OOZE restart: 6.32 s off the object | reprime 4.512 mm | boost 1.000 mm over 15 mm`.

### Reading the ladder

Each cylinder restarts at the back, on the side facing its tower, and prints its first loop from there. Look at the
first part of the loop after the restart on each layer:

- **Starved** (thin or missing line): too little reprime, or not enough settle for the pressure to build.
- **Blob or bulge** at the restart: too much reprime.
- **Even line**: the right reprime for that row's wait.

Go row by row (front to back is shortest to longest wait) and note the column with the even line, and the reprime
in its name. If the same column wins in every row, its rate is the nozzle's ooze rate and the base reprime is right:
a restart after a wait of t seconds needs the base plus rate times t. If the short waits want a faster column than
the long ones, raise the base reprime; if they want a slower one, lower it. Either way, the winning reprimes against
their waits make a straight line whose slope is the ooze rate and whose start is the base. If every column of a row
is starved, run it again with higher rates or a longer settle; if every column blobs, use lower rates.

The cylinders are as large as the bed allows, up to 25 mm. Four rows of towers leave room for about 11 mm, which is
only a few lines across with a large nozzle, so use the first run to find the region that works, then run the ladder
again with three waits and a narrower range of rates around it for cylinders twice that size.

### Weighing the cylinders

The seams only show where the reprime lands. How much the nozzle lost shows in the weights, and that is what the ooze
curve is fitted to:

1. Rename one cylinder to anything that doesn't start with `ooze`, for example `control`. It prints with no stops, so
   its weight is what a cylinder weighs when nothing is lost. Keep each cylinder with its own tower when you take
   them off, drips in the hollow included.
2. Weigh a few short pieces of the filament, measured with calipers, for its real milligrams per millimetre; the
   density on a data sheet is often for molded test bars.
3. A cylinder's weight minus the control's, divided by its layers and by the filament's mg/mm, is how many mm of
   filament its reprime fell short (negative) or over. Its reprime plus that shortfall is what the nozzle lost in
   that row's time off the cylinder, and its tower should have gained about the same.

The losses for each row's real time off fit a curve that rises quickly and levels off: the **maximum ooze** and the
**ooze half-life** under **Others > Ooze reprime**. The boost ladder and normal prints both reprime by that curve.

The drips in each tower's hollow are the ooze itself: everything that came out during that row's waits. Weighing
them gives a check on the ladder's rate: mm of filament per second = grams / density in g/cm³ × 1000 / filament
cross-section in mm² (2.405 for 1.75 mm filament) / total seconds waited (layers × wait). What still hangs from the
nozzle is wiped onto the wall instead, so this reads a little low.

Waits use `M400 S<seconds>` on Bambu printers, as Bambu's own G-code does, and `G4 P<milliseconds>` elsewhere.

### Object names

The test settings live in the object names, so a saved project keeps them, and any object can be given stops by
renaming it. A name that starts with the word `ooze` takes these tokens, separated by spaces or underscores:

| Token | Meaning |
| --- | --- |
| `W30` | Wait 30 seconds (required) |
| `R1.2` | Reprime 1.2 mm of filament on restart (default 0) |
| `R0+0.5/3` | Reprime 0 mm, raised by 0.5 mm every 3 layers of the object |
| `A` | Instead of R, reprime what the ooze curve in the print settings gives for the real time off the object |
| `B1.5` | Start boost: push 1.5 mm more along the first **Boost distance** of the line, tapering off |
| `C20` | Cool the nozzle by 20 °C while waiting (default 20 over a tower or the chute, 0 when hovering) |
| `D2` | Retract 2 mm further while waiting |
| `S1` | After the reprime, wait 1 second for the pressure to build before printing |
| `T` | Wait over an ooze tower behind the object and print the tower before restarting |
| `H` | Hover over the restart point instead |
| `P` | Purge the reprime into the tower or chute, then restart with the normal extra length |

Without `T` or `H`, the nozzle parks over the purge chute. For example: `ooze W30 R3 S1 T`, `ooze W30 R1.5 C40 D2 T`,
`ooze W20 R0+0.5/3 C0 T`, `ooze W60 R1 H`, `ooze W4 A B2 T`.

An object with `T` needs room behind it (towards the back of the bed) for its tower, and cannot have a raft, because
the tower starts on the bed. Slicing stops with an error if a tower is off the bed or overlaps another object or tower.

Files sliced with stops carry `; OOZE_POST_DONE native` in the header, so the older `OozePost.bat`
post-processing script leaves them alone. The code is in `src/libslic3r/GCode/OozeStop.*`, `GCode/OozeTower.*` and
the `ooze_*` functions in `GCode.cpp`. The tests are tagged `[OozeStop]`.

## Ooze control for normal prints

Two groups under **Print settings > Others** use the ooze ladder's results in normal prints.

### Ooze reprime

**Reprime after travels** pushes back what the nozzle oozed while it was off the print. After a retracted travel of
at least **Minimum travel time**, printing restarts with

    reprime = Maximum ooze × (1 − 0.5^(time off / Ooze half-life))

in place of the extra length on restart. The time off runs from the retraction to the restart, and is worked out from
the G-code the travel writes: the retraction and wipe, the Z hop, the travel with its acceleration, and any detour
that **Avoid crossing walls** adds. The travel from the start G-code to the first part is not counted.

The reprime is pushed after the retraction goes back, at half the filament's maximum volumetric speed, so it melts as
it goes in. A large nozzle still starts its line thin while the pressure builds, so the **Start boost** then pushes
more filament along the first **Boost distance** of the line: most at the restart point, tapering to nothing. Where
the line plus the boost would push more than the filament's maximum volumetric speed, that stretch slows down. The
boost carries on across paths until the next travel or retraction. **Settle after the reprime** waits at the restart
point before the nozzle moves; whatever comes out meanwhile stays there as a blob, so the boost usually does better.

From the calibration: **Maximum ooze** and **Ooze half-life** come from the weighed ladders, and **Start boost** and
**Boost distance** from the boost ladder.

Travels that stay inside the part leave their drips on the infill. Turn on **Avoid crossing walls** so travels stay
inside the part, and **Reduce infill retraction** if the drips should go into the infill without a retraction. Use
the inner/outer wall order so restarts land on an inner wall rather than the outer one.

### Ooze tower

**Ooze tower** adds one ooze tower to the print, at the X and Y given there; place it next to the part. A travel goes
through it when it would take longer than **Travel time**, counting the travel acceleration, and leaves the inside of
the part (travels that stay over infill hide their drips there). This is for parts with little infill, mostly walls,
or separate sections far apart.

On each visit the nozzle:

1. Prints one loop of the tower's current layer, which wipes off what it oozed on the way.
2. Retracts and waits over the tower's hollow for the **Drain time**, so it oozes there instead of on the part. About
   four half-lives empties it.
3. Comes down and wipes across the tower's walls towards where it is going.
4. Travels on to the part and restarts with the reprime the curve gives for the whole time away, then the start
   boost. This happens even when **Reprime after travels** is off.

At the end of every layer the loops the layer has left are printed, so the tower grows with the print and is always
solid. Its first layer has the brim loops for grip. The tower needs layer-by-layer printing (not by object) and does
not work in spiral vase mode. The tests are tagged `[OozeTower]` and `[OozeReprime]`.

## Odd nozzle sizes on Bambu Lab printers

A Bambu Lab printer only takes the nozzle sizes it knows (0.2, 0.4, 0.6 and 0.8 mm). It refuses a sliced file
whose nozzle size doesn't match the size set on the printer, so a 1.8 mm nozzle can't be printed as it is.

Turn on **Printer settings > Basic information > Advanced > Report the variant's nozzle to the printer** in the
printer preset of the big nozzle. Sliced files then tell the printer it has the nozzle of the preset's variant
(0.4 mm for a 0.4 variant, and 0.4 mm when the variant isn't a number) instead of the real one. Set the printer
to that size too, with the same nozzle flow type (Standard or High Flow) as the preset. Slicing still uses the real
nozzle diameter for line widths, flow and everything else.

What the option changes, and only in sliced files (`.gcode` and `.gcode.3mf`), never in saved projects:

- `nozzle_diameter` in the G-code config block.
- The `.gcode.3mf` slice info (`nozzle_diameters` and each filament's `nozzle_diameter`), project settings and
  plate data.
- The nozzle the **Print** dialog compares with the printer's.

This replaces the `Bambu Nozzle Fix.bat` relabelling script. The code is in `src/libslic3r/ReportedNozzle.*` and
the tests are tagged `[ReportedNozzle]`.

## Next update list

- **Fit the Ooze and reprime dialog on one screen.** With three tests the dialog is taller than a laptop or single
  monitor, so the lower fields and the OK button end up off screen. Show only the selected test's box (Ooze ladder,
  Reprime check or Boost ladder) and hide the others instead of greying them out, and refit the dialog when the test
  changes.

- **Wipe over infill before a travel.** Before a long travel inside the part, move the nozzle over the nearest
  internal infill and wipe there, rather than along the last path (which can be the outer wall), so nothing it
  carries ends up on a wall.

- **Show the ooze towers in the Prepare view.** Draw the print's ooze tower and the towers behind ooze test objects on
  the plate, let the print's tower be dragged into place like the prime tower, and have Arrange leave room for them.
  Today they only show after slicing, and slicing reports an overlap.

- **Import settings from other OrcaSlicer forks.** A popup that finds every OrcaSlicer-family settings folder
  (OrcaSlicer, OrcaSlicer WaveOverhangs and other forks) and lets you pick which ones to copy printer, filament and
  process presets from. Show it on first launch and from the File menu. Notes from the first install:
  - The current first-launch import never showed. It only runs when the settings folder doesn't exist yet, but the
    folder already exists by then (the bundled Python cache and machine id are written to it first). Base "first
    launch" on there being no user presets or no `OrcaGarageBuild.conf` instead.
  - Copy only the `user/<id>/{machine,filament,process}` presets, and skip `.bak` files, rather than the whole folder.
  - Say which printer vendors the copied presets need (Sovol, for example) so they can be enabled in printer
    setup. Presets whose vendor isn't enabled don't show up.

## Builds

The **GarageBuild Windows** workflow builds the Windows x64 installer and portable zip on every push to `main`.
Then it runs the `[OozeStop]` and `[ReportedNozzle]` tests. Download the build from the run's **Artifacts**. The full upstream
`build_all.yml` (every platform) runs only when started from the Actions tab.

## Updating from upstream

```bash
git remote add upstream https://github.com/OrcaSlicer/OrcaSlicer.git   # once
git fetch upstream
git merge upstream/main
```

Fork changes are marked with `OrcaGarageBuild` or `Orca: wave-overhang` comments. Conflicts usually come up in
`GCode.cpp`, `PrintConfig.*`, `PerimeterGenerator.*`, `Fill.cpp`, `CoolingBuffer.cpp`, `Preset.cpp` and the CMake
and version files. Take upstream's side for `resources/profiles` and `.github` (except `garage_windows.yml` and the
two small edits to `build_all.yml` and `build_orca.yml`).
