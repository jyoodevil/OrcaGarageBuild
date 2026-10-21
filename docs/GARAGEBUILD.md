# OrcaGarageBuild

OrcaGarageBuild is a personal test fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer). It is upstream
OrcaSlicer with two additions:

- **Wave overhangs** from [OrcaSlicer-WaveOverhangs](https://github.com/dennisklappe/OrcaSlicer-WaveOverhangs)
  (settings: [WAVE_OVERHANG_SETTINGS.md](WAVE_OVERHANG_SETTINGS.md)).
- **Ooze and reprime calibration**, for large nozzles that lose pressure while they wait.

It installs as *OrcaGarageBuild* (`orca-garagebuild.exe`). It keeps its settings in its own folder
(`%APPDATA%\OrcaGarageBuild` on Windows), so it never touches an existing OrcaSlicer install. On first launch it
offers to copy profiles from OrcaSlicer WaveOverhangs, OrcaSlicer, Bambu Studio or PrusaSlicer. The same import is
under **File > Import > Import from other slicer**.

## Ooze and reprime calibration

A big nozzle keeps oozing after a retraction, so after a stop the restart is short of material. How much it loses
depends on how long it waited. This calibration measures that, so the restart can push the right amount back in.

Open **Calibration > Ooze and reprime**. There are two tests:

- **Ooze ladder**: a grid of short hollow cylinders. Rows run front to back by wait time, and columns run left to
  right by reprime length. Before every layer of every cylinder, the nozzle stops for that row's wait, then restarts
  with that column's reprime length. Look at the seam on the back of each cylinder: a gap means too little reprime
  and a blob means too much. The best column in each row is the reprime for that wait.
- **Reprime check**: one row of taller cylinders that all wait the same time, with reprime lengths around one
  value. Use it to confirm a value from the ladder over many layers. It can also purge the reprime into the chute
  and restart normally, which tests priming at the chute instead of at the part.

Reprime lengths are millimetres of filament pushed back in after the stop. They replace the *extra length on
restart* (`retract_restart_extra`) for that restart only.

### Stops

- **Park** (Bambu Lab X1 and P1): the nozzle retracts, lifts 3 mm above everything printed so far and parks over
  the purge chute. It cools by the chosen amount while it waits, then reheats. It shakes off and wipes on the
  silicone wiper the way Bambu's filament change does, comes back to the restart point and pushes the reprime.
- **Hover** (any printer): the nozzle retracts, lifts 1 mm or one layer height, whichever is more, and waits above
  the restart point, as it would during a long travel. Then it comes back down and pushes the reprime.

Waits use `M400 S<seconds>` on Bambu printers, as Bambu's own G-code does, and `G4 P<milliseconds>` elsewhere.

### Object names

The test settings live in the object names, so a saved project keeps them, and any object can be given stops by
renaming it. A name that starts with the word `ooze` takes these tokens, separated by spaces or underscores:

| Token | Meaning |
| --- | --- |
| `W30` | Wait 30 seconds (required) |
| `R1.2` | Reprime 1.2 mm of filament on restart (default 0) |
| `R0+0.5/3` | Reprime 0 mm, raised by 0.5 mm every 3 layers of the object |
| `C20` | Cool the nozzle by 20 °C while parked (default 20 when parked, 0 when hovering) |
| `H` | Hover over the restart point instead of parking |
| `P` | Purge the reprime into the chute, then restart with the normal extra length |

For example: `ooze W30 R1.5`, `ooze W20 R0+0.5/3 C0`, `ooze W60 R1 H`.

Files sliced with stops carry `; OOZE_POST_DONE native` in the header, so the older `OozePost.bat`
post-processing script leaves them alone. The code is in `src/libslic3r/GCode/OozeStop.*` and the `ooze_*`
functions in `GCode.cpp`. The tests are tagged `[OozeStop]`.

## Next update list

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
Then it runs the `[OozeStop]` tests. Download the build from the run's **Artifacts**. The full upstream
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
