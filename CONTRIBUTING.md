# Contributing

## Workflow
1. Create a branch from `main`.
2. Keep changes scoped to one concern.
3. Build `IcarusEditor` before opening a PR.
4. If touching generation/export logic, validate in-editor and attach output evidence.
5. Open a PR with problem statement, approach, and validation evidence.

## Coding Standards
- Follow Unreal C++ conventions (`F` types, `E` enums, `b` bool prefix, PascalCase functions).
- Keep module boundaries clear (`PTPCore` for data/math, `PTPRuntime` for rendering/editor integration).
- Avoid simulation logic in rendering paths.

## Commit Messages
Use conventional-style summaries:
- `feat(ptpruntime): add planet actor mesh upload`
- `fix(ptpcore): correct adjacency symmetry check`
- `chore(repo): update github templates`

## PR Checklist
- Build passes locally.
- No generated folders committed (`Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`).
- New behavior is documented in `README.md` when relevant.
- For map/export changes, include at least one output image from `Saved/TectonicMaps/` in the PR description.
