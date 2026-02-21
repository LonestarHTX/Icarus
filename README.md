# Icarus

Procedural tectonic planet generation in Unreal Engine 5.7, based on the 2019 paper **Procedural Tectonic Planets** (Cortial, Peytavie, Galin, Guerin).

## Current Status
- Step 1: Fibonacci sphere sampling, spherical Delaunay triangulation, CSR adjacency, geometry validation.
- Step 2: Runtime rendering actor (`APlanetActor`) with RealtimeMesh Pro, visualization modes, and editor-time generation.

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

## Branching
- `main` stays buildable.
- Use short-lived feature branches (`feat/<name>`, `fix/<name>`).

