# OrcaGarageBuild

OrcaGarageBuild is a personal test fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer). It is upstream
OrcaSlicer with these additions:

- **Wave overhangs** from [OrcaSlicer-WaveOverhangs](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs)
  (settings: [WAVE_OVERHANG_SETTINGS.md](WAVE_OVERHANG_SETTINGS.md)).
- **Ooze and reprime calibration**, for large nozzles that lose pressure while they wait.
- **Ooze tower**, which cleans and primes the nozzle on long travels.
- **Report the variant's nozzle to the printer**, for nozzle sizes a Bambu Lab printer can't be set to.

It installs as *OrcaGarageBuild* (`orca-garagebuild.exe`). It keeps its settings in its own folder
(`%APPDATA%\OrcaGarageBuild` on Windows), so it never touches an existing OrcaSlicer install. On first launch it
offers to copy profiles from OrcaSlicer WaveOverhangs, OrcaSlicer, Bambu Studio or PrusaSlicer. The same import is
under **File > Import > Import from other slicer**.

## Ooze and reprime calibration

A big nozzle keeps oozing after a retraction. What drips out while it waits ends up on the part when it comes back,
and the restart is short of material by however much it lost. This calibration measures both, so the restart can
push the right amount back in and a print knows when to clean the nozzle on an ooze tower first.

Open **Calibration > Ooze and reprime**. There are two tests:

- **Ooze ladder**: a grid of short hollow cylinders. Rows run front to back by wait time, and columns run left to
  right by reprime length. Before every layer of every cylinder, the nozzle stops for that row's wait, then restarts
  with that column's reprime length. Look at the seam on the back of each cylinder: a gap means too little reprime
  and a blob means too much. The best column in each row is the reprime for that wait.
- **Reprime check**: one row of taller cylinders that all wait the same time, with reprime lengths around one
  value. Use it to confirm a value from the ladder over many layers. It can also purge the reprime into the tower or
  chute and restart normally, which tests priming there instead of at the part.

Reprime lengths are millimetres of filament pushed back in after the stop. They replace the *extra length on
restart* (`retract_restart_extra`) for that restart only.

### Stops

- **Over an ooze tower** (any printer, the default): each cylinder gets its own ooze tower right behind it. The
  nozzle retracts, lifts and waits over the tower's hollow centre, so what it drips falls inside the tower. It cools
  by the chosen amount while it waits, then reheats fully and prints the tower's layer from the inside out, which
  wipes off what still hangs from the nozzle and builds the pressure back up. Then it goes back to the cylinder and
  pushes the reprime. The towers show up when the plate is sliced; their size comes from the print settings under
  **Others > Ooze tower**.
- **Park** (Bambu Lab X1 and P1): the nozzle parks over the purge chute, cools while it waits, then shakes off and
  wipes on the silicone wiper the way Bambu's filament change does. This needs the wiper in place.
- **Hover** (any printer): the nozzle lifts 1 mm or one layer height, whichever is more, and waits above the restart
  point, as it would during a long travel. Use it with short waits to find how long a travel can be before the
  restart suffers; that is the ooze tower's **Travel time** for normal prints.

Every stop can also retract further while it waits (**Retract further while waiting**, 2 mm by default) and push it
back before printing again. A deeper retraction oozes less, but too deep can pull molten filament into the cold zone.

Waits use `M400 S<seconds>` on Bambu printers, as Bambu's own G-code does, and `G4 P<milliseconds>` elsewhere.

### Object names

The test settings live in the object names, so a saved project keeps them, and any object can be given stops by
renaming it. A name that starts with the word `ooze` takes these tokens, separated by spaces or underscores:

| Token | Meaning |
| --- | --- |
| `W30` | Wait 30 seconds (required) |
| `R1.2` | Reprime 1.2 mm of filament on restart (default 0) |
| `R0+0.5/3` | Reprime 0 mm, raised by 0.5 mm every 3 layers of the object |
| `C20` | Cool the nozzle by 20 °C while waiting (default 20 over a tower or the chute, 0 when hovering) |
| `D2` | Retract 2 mm further while waiting |
| `T` | Wait over an ooze tower behind the object and print the tower before restarting |
| `H` | Hover over the restart point instead |
| `P` | Purge the reprime into the tower or chute, then restart with the normal extra length |

Without `T` or `H`, the nozzle parks over the purge chute. For example: `ooze W30 R1.5 C40 D2 T`,
`ooze W20 R0+0.5/3 C0 T`, `ooze W60 R1 H`.

An object with `T` needs room behind it (towards the back of the bed) for its tower, and cannot have a raft, because
the tower starts on the bed. Slicing stops with an error if a tower is off the bed or overlaps another object or tower.

Files sliced with stops carry `; OOZE_POST_DONE native` in the header, so the older `OozePost.bat`
post-processing script leaves them alone. The code is in `src/libslic3r/GCode/OozeStop.*`, `GCode/OozeTower.*` and
the `ooze_*` functions in `GCode.cpp`. The tests are tagged `[OozeStop]`.

## Ooze tower for normal prints

**Print settings > Others > Ooze tower** adds one ooze tower to a normal print, at the X and Y given there. A travel
goes through it when all of these hold:

- the travel takes longer than **Travel time** at the travel speed;
- it leaves the inside of the part (travels that stay over internal infill hide their strings);
- the tower is close enough that the trip from the tower to where printing resumes is shorter than **Travel time**.
  Otherwise the visit would not help, so place the tower next to the part.

Each visit prints one loop of the tower's current layer, which wipes the nozzle and builds its pressure back up, then
the nozzle goes on to the part and restarts with **Reprime after the tower** in place of the extra length on
restart. At the end of every layer the loops the layer has left are printed, so the tower grows with the print and
is always solid. Its first layer has the brim loops for grip. The tower needs layer-by-layer printing (not by object)
and does not work in spiral vase mode.

Set the values from the calibration: **Travel time** from the ladder in Hover mode (the longest wait whose seam is
still clean with some reprime), and **Reprime after the tower** from the ladder with towers.

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
