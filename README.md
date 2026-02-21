# Icarus

Procedural tectonic planet generation in Unreal Engine 5.7, based on the 2019 paper **Procedural Tectonic Planets** (Cortial, Peytavie, Galin, Guerin).

## Current Status
- Step 1: Fibonacci sphere sampling, spherical Delaunay triangulation, CSR adjacency, geometry validation.
- Step 2: Runtime rendering actor (`APlanetActor`) with RealtimeMesh Pro, visualization modes, and editor-time generation.
- Step 3: Plate initialization (`~40` plates), seeded Voronoi-style flood fill, crust/elevation initialization, and per-plate angular motion metadata.
- Step 4: Geodetic plate drift (`DeltaT = 2 My`) with per-plate rigid rotation, parallel sample updates, simulation stepping, and playback controls.
- Step 5: Boundary detection/classification (convergent/divergent/transform), per-sample boundary metadata, and per-plate `DistToFront` geodesic distance field.
- Map Export: Equirectangular PNG export layers (`PlateID`, `Elevation`, `ContinentalMask`, `BoundaryType`, `Velocity`, `Composite`) with console commands.

## Repository Layout
- `Source/Icarus/`: game module.
- `Plugins/ProceduralTectonicPlanets/`: tectonic plugin (`PTPCore`, `PTPSimulation`, `PTPRuntime`).
- `Plugins/RealtimeMeshComponent/`: rendering dependency.
- `Config/`, `Content/`: Unreal project config and assets.

## Build
From WSL, build via Windows toolchain:

```bash
powershell.exe -NoProfile -Command "& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' IcarusEditor Win64 Development '-Project=C:\Users\Michael\Documents\Unreal Projects\Icarus\Icarus.uproject' -WaitMutex -NoHotReload -NoUBTMakefiles"
```

## First Run
1. Open `Icarus.uproject` in UE 5.7.
2. Place `APlanetActor` in a level.
3. Assign a vertex-color material (unlit recommended).
4. Click **Generate Planet** in Details.

## Map Export
Use editor console commands:

```text
TectonicExport.All
TectonicExport.All 4096 2048
TectonicExport.Layer Elevation
TectonicExport.Layer Composite 4096 2048
```

Outputs are written to `Saved/TectonicMaps/`.

## Simulation Controls
Use editor console commands:

```text
Tectonic.Step
Tectonic.Step 10
Tectonic.Play
Tectonic.Stop
Tectonic.Reset
```

`Tectonic.Step N` advances by `N * 2 My`, updates the mesh, and exports map layers to `Saved/TectonicMaps/` when enabled on `APlanetActor`.
Each step now logs boundary summary stats (`segments`, `boundary sample ratio`, `DistToFront` min/max/mean).

## Branching
- `main` stays buildable.
- Use short-lived feature branches (`feat/<name>`, `fix/<name>`).
