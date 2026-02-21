# Repository Guidelines

## Project Structure & Module Organization
This is an Unreal Engine 5.7 project (`Icarus.uproject`). Core game module code is in `Source/Icarus/`. Engine/project config lives in `Config/`, runtime assets in `Content/`, and generated outputs in `Binaries/`, `Intermediate/`, and `Saved/`.

Plugins are under `Plugins/`:
- `Plugins/RealtimeMeshComponent/`: third-party plugin dependency.
- `Plugins/ProceduralTectonicPlanets/`: in-repo plugin with four modules:
  - `PTPCore` (math/data, triangulation, adjacency)
  - `PTPSimulation` (plate initialization + simulation-layer logic)
  - `PTPRuntime` (runtime actor rendering, map export, console commands)
  - `PTPRuntimeEditor` (editor camera control panel integration)

Keep public headers in `Public/` and implementation in `Private/` per Unreal module conventions.

## Build, Test, and Development Commands
Run builds from WSL via Windows PowerShell:

```bash
powershell.exe -NoProfile -Command "& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' IcarusEditor Win64 Development '-Project=C:\Users\Michael\Documents\Unreal Projects\Icarus\Icarus.uproject' -WaitMutex -NoHotReload -NoUBTMakefiles"
```

Regenerate solution/project files when module layout changes:

```bash
powershell.exe -NoProfile -Command "& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' -ProjectFiles '-Project=C:\Users\Michael\Documents\Unreal Projects\Icarus\Icarus.uproject'"
```

Use Unreal Editor for runtime verification and log inspection (`Saved/Logs/Icarus.log`).
Map exports are written to `Saved/TectonicMaps/` via `TectonicExport.All` and `TectonicExport.Layer`.

## Coding Style & Naming Conventions
- Use Unreal C++ style: tabs/spaces as in surrounding files, include order stable, minimal comments.
- Types: `F` structs/classes, `E` enums, `b` boolean prefix, PascalCase for functions.
- Constants: `constexpr` in namespace scope (see `PTPCore/Public/PlanetConstants.h`).
- Keep module dependencies explicit in each `*.Build.cs`.

## Testing Guidelines
No dedicated test module is currently checked in. Validate changes by:
- Building `IcarusEditor` without warnings/errors.
- Running geometry validation paths in `PTPCore` and checking `UE_LOG` output.
- For export changes, run at least one `TectonicExport.Layer` command and verify no banding/artifacts.
- Adding focused tests later via Unreal Automation Framework when simulation behavior is introduced.

## Commit & Pull Request Guidelines
Git history is not available in this workspace snapshot, so use this convention:
- Commit format: `type(scope): summary` (e.g., `feat(ptpcore): add CSR adjacency builder`).
- Keep commits narrow and buildable.
- PRs should include: goal, key files changed, build/test evidence, and screenshots only for editor/UI changes.
- Link related task/issue IDs and note any follow-up work explicitly.
