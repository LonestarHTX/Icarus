# Resampling Architecture Analysis: Procedural Tectonic Planet Generator

**Date:** 2026-02-22
**Context:** UE5.7 C++ editor plugin implementing Cortial et al. 2019 "Procedural Tectonic Planets"
**Problem:** Progressive continental degradation after ~120 My of simulation (6 resample cycles)

---

## Task 1: Paper's Resampling Description

### What the Paper Says

The paper's Section 6 (Implementation Details) contains the key passage on resampling:

> "The sampling and meshing of the sphere is performed off-line as a pre-processing step, and re-used many times during the simulation, in particular for seafloor generation. Instead of incrementally remeshing the planet at every step, which would be computationally demanding, we compute the movement of the plates and perform a global resampling every 10–60 iterations. The parameters of the samples located between diverging plates are computed using the method described in Section 4.3. The parameters of the other sample points are computed using barycentric interpolation of crust data from the plate they intersect. The new set of plates is built by partitioning the triangulation according to samples assignments."

Section 3 describes the spherical mesh:
- Points pk are sampled on the sphere using a Fibonacci lattice
- A spherical Delaunay triangulation M is computed on these points
- The triangulation is the convex hull of points on the unit sphere (since all points lie on a sphere, their 3D convex hull produces the Delaunay triangulation)
- Each plate is defined as a connected subset of this triangulation

Table 1 lists the crust parametrization per sample: elevation z, thickness e, crust type τ (continental/oceanic), oceanic age a, orogeny type, orogeny age, fold direction, ridge direction, and distance to convergent front.

### Answers to Specific Questions

**Q1: What does "the plate they intersect" mean?**

The phrase "barycentric interpolation of crust data **from the plate they intersect**" is the critical design statement. It means: each new Fibonacci point is assigned to exactly one plate, and its crust parameters are interpolated **only from data belonging to that plate**. The paper does NOT describe cross-plate interpolation. The word "intersect" here means "the plate whose territory the new sample point falls within" — not "the triangle they fall inside."

This is the single most important distinction between the paper's method and our current implementation.

**Q2: How does the paper build per-plate meshes for interpolation?**

The paper describes a single global triangulation M of the Fibonacci points, reused across the simulation. The phrase "triangulations M of points pk" (Section 3) refers to this global triangulation. Plates are subsets of this triangulation — the "partitioning" step assigns each sample to a plate, and edges between same-plate samples form the plate's sub-mesh.

However, for interpolation purposes during resampling, the paper says "from the plate they intersect," which implies that the interpolation sources are filtered to same-plate vertices. There are two ways to achieve this:

1. **Per-plate Delaunay triangulations** — build a separate triangulation for each plate's vertices, then query each new point against the assigned plate's triangulation.
2. **Global triangulation with same-plate vertex filtering** — use the global (old, moved) triangulation for point location, but only interpolate from vertices belonging to the assigned plate, renormalizing barycentric weights.

The paper does not specify which approach they use. Given their performance numbers (13.1s for 500k points, Table 2), either approach is feasible.

**Q3: What happens at plate boundaries where a new sample falls in a cross-plate triangle?**

The paper does not explicitly address this edge case. However, the statement "from the plate they intersect" implies that cross-plate blending is not performed. At boundaries, triangles in the old (moved) global triangulation will have vertices from different plates. The paper's approach would either:

- Filter to same-plate vertices within that triangle and renormalize, OR
- Fall back to a per-plate triangulation or nearest-neighbor lookup

The paper treats divergent gaps specially (Section 4.3), so the remaining cross-plate triangles are at convergent and transform boundaries. At these boundaries, the paper likely uses same-plate filtering, since blending continental and oceanic data would produce the exact degradation we observe.

**Q4: Is barycentric interpolation done on the global triangulation or per-plate sub-meshes?**

Given the paper's emphasis on a single pre-computed Fibonacci lattice reused across the simulation, the most likely approach is: use the global (old, moved) triangulation for geometric point location (finding which triangle contains the new point), then filter interpolation sources to the assigned plate. This avoids the complexity of building per-plate Delaunay triangulations while still preventing cross-plate blending.

**Confidence level:** High on the interpretation that the paper intends same-plate-only interpolation. Medium on the exact mechanism (filtering vs. per-plate triangulation), since the paper is underspecified on this implementation detail.

---

## Task 2: Current Implementation Analysis

### Overview of the Resampling Pipeline

`GlobalResample()` (Resampling.cpp:676–790) follows this sequence:

1. **Cache Fibonacci lattice** (line 686): Generate or reuse a fixed Fibonacci sphere + Delaunay triangulation + adjacency. This is the NEW mesh — the mesh that the resampled data will live on.
2. **Build spatial grid** (line 693): 64³ grid of old sample positions for nearest-neighbor queries.
3. **Build global BVH** (line 700): BVH over the OLD (moved) global triangulation for point-in-triangle queries.
4. **ParallelFor over new sample positions** (line 714–750):
   - Find containing triangle in OLD triangulation via BVH
   - If no triangle found → `BuildGapSample()` (divergent gap)
   - If triangle is a gap triangle (cross-plate, ≥2 divergent boundary vertices, edges > threshold) → `BuildGapSample()`
   - Otherwise → `ResolveWinningPlate()` then `InterpolateFromTriangle()`
5. **Replace old samples** (line 753–757): Move new samples into state, copy cached lattice's triangulation/adjacency.
6. **Rebuild plate metadata** (line 759): Recalculate plate areas, seed indices, crust types.
7. **Detect boundaries** (line 762): Run `DetectAndClassifyBoundaries()` on the new state.

### Q1: What happens in InterpolateFromTriangle() for cross-plate triangles?

`InterpolateFromTriangle()` (Resampling.cpp:526–564) performs **unconditional interpolation from all three vertices** regardless of plate membership.

Given the example: vertex A (plate A, continental, +0.5 km) weight 0.3; vertex B (plate A, continental, +0.3 km) weight 0.4; vertex C (plate B, oceanic, -4.0 km) weight 0.3:

```
Out.Elevation = 0.3 × 0.5 + 0.4 × 0.3 + 0.3 × (-4.0)
              = 0.15 + 0.12 + (-1.20)
              = -0.93 km
```

The new sample is **assigned to plate A** (via `ResolveWinningPlate`), and its **CrustType is Continental** (from vertex B, the majority-weight vertex at 0.4). But its **elevation is -0.93 km** — deep underwater. This creates a continental sample with an oceanic-depth elevation.

Similarly:
```
Out.Thickness = 0.3 × 35.0 + 0.4 × 35.0 + 0.3 × 7.0
              = 10.5 + 14.0 + 2.1
              = 26.6 km
```

Continental thickness should be ~35 km, but it's been dragged down to 26.6 km. While thickness isn't directly used in downstream calculations that produce visible artifacts, it illustrates the systematic property corruption.

```
Out.OceanicAge = 0.3 × 0.0 + 0.4 × 0.0 + 0.3 × (age_of_oceanic_sample)
```

Continental samples don't have meaningful oceanic age (typically 0), but after blending, they acquire nonzero oceanic age from the oceanic vertex.

### Q2: Cumulative effect over 6 resample cycles

This is the **core mechanism of continental degradation**. Consider a continental edge sample at +0.5 km that shares a cross-plate triangle with an oceanic sample at -4.0 km:

**Cycle 1:** Elevation drops from +0.5 to approximately -0.93 (as computed above).

**Cycle 2:** The now-degraded sample at -0.93 participates in interpolation for neighboring continental samples. The "contaminated zone" expands inward. Meanwhile, the sample itself may be in a new cross-plate triangle and gets further degraded.

**Cycle 3–6:** The effect cascades inward. Each cycle:
- Boundary samples lose elevation from cross-plate blending
- Some boundary samples get misclassified (CrustType=Continental but elevation is negative)
- These misclassified samples contaminate their neighbors in the next cycle
- The boundary zone widens by roughly one triangle edge length per cycle

After 6 cycles (at ~20 My intervals over 120 My), the contaminated zone has expanded **6 edge-widths inward** from the original plate boundary. For 500k samples, the average edge length is approximately:

```
Average spacing ≈ √(4π × 6370² / 500000) ≈ √(1019) ≈ 32 km
```

So after 6 cycles, ~192 km of continental coastline is degraded. For smaller continents (radius ~1000 km), this represents a significant fraction of the landmass. The degradation manifests as:
- **Edge erosion:** Continent edges eaten inward
- **Elevation depression:** Coastal elevations pulled toward oceanic values
- **Dithering:** CrustType mismatches create a noisy coastline pattern where continental and oceanic samples are interleaved

### Q3: How does ResolveWinningPlate() determine plate assignment?

`ResolveWinningPlate()` (Resampling.cpp:500–524) uses this logic:

1. If any two vertices share the same plate → that plate wins (majority rule)
2. If all three vertices have different plates → highest barycentric weight wins

**This assignment is used ONLY to set `Out.PlateIndex` in `InterpolateFromTriangle()` (line 555).** It does NOT filter the interpolation. The elevation, thickness, oceanic age, orogeny age, ridge direction, and fold direction are ALL interpolated from all three vertices regardless of plate.

This is the root cause disconnect: the plate assignment logic correctly identifies the winning plate, but then the interpolation ignores this information entirely and blends data from all plates.

### Q4: How does BuildGapSample() work?

`BuildGapSample()` (Resampling.cpp:566–628) is triggered in two cases:
1. **No containing triangle found** in the BVH (line 719–724) — the new point falls outside all old triangles (in a gap where plates have separated)
2. **Gap triangle detected** (line 733–742) — a cross-plate triangle with ≥2 divergent boundary vertices and edges longer than the gap threshold

`BuildGapSample()`:
- Creates a fresh oceanic sample (CrustType=Oceanic, Thickness=7.0, OceanicAge=0.0)
- Finds the two nearest old samples from distinct plates via `FindNearestSamplesDistinctPlates()`
- Computes a ridge profile based on distance from the midpoint between the two plates
- Elevation follows: `RidgeElevation + (AbyssalElevation - RidgeElevation) × √T` where T is normalized distance from ridge
- Assigns the sample to the nearer plate

This function is **correctly designed** for divergent boundaries. The problem is NOT with gap samples — it's with the "normal" interpolation path for non-gap cross-plate triangles.

### Q5: How does post-resample boundary detection interact with noisy plate boundaries?

After resampling, `DetectAndClassifyBoundaries()` is called (Resampling.cpp:762). The detection works by checking each sample's adjacency neighbors on the NEW triangulation (BoundaryDetection.cpp:329–343): a sample is flagged as a boundary if ANY neighbor belongs to a different plate.

**The interaction with noisy boundaries is a vicious cycle:**

1. Cross-plate interpolation creates samples with mismatched plate/elevation properties
2. After resampling, plate boundaries are determined by the `PlateIndex` field (set by `ResolveWinningPlate`)
3. `ResolveWinningPlate` at boundaries produces noisy plate assignments — a sample in a 2-vs-1 triangle gets the majority plate, but a nearby sample in a 1-vs-2 triangle might get the other plate
4. This creates a "fuzzy" boundary zone where plate assignments alternate
5. Boundary detection flags more samples as boundaries than the true geometric boundary
6. More boundary samples → more samples with contaminated interpolation in the next cycle
7. The boundary zone widens each cycle

Additionally, `ComputeDistanceToFront()` (BoundaryDetection.cpp:159–307) uses Dijkstra BFS from convergent boundary samples, propagating only to same-plate neighbors. If boundary samples are noisy, the distance field becomes noisy too, affecting subduction uplift distribution in the next simulation step.

---

## Task 3: All Possible Sources of Continental Degradation

### Source 1: Cross-plate elevation blending (PRIMARY CAUSE)

**Severity: CRITICAL**

As demonstrated in Task 2 Q1, `InterpolateFromTriangle()` blends elevation from all three vertices regardless of plate. Continental samples at plate boundaries get their elevation pulled toward oceanic values (-4 to -6 km). This is cumulative and progressive.

**Evidence:** A continental sample at +0.5 km in a cross-plate triangle with an oceanic vertex at -4.0 km gets elevation of approximately -0.93 km in a single resample cycle. After multiple cycles, this creates an expanding zone of depressed-elevation continental samples.

**Affected lines:** Resampling.cpp:548 (`Out.Elevation = W1 * S1.Elevation + W2 * S2.Elevation + W3 * S3.Elevation`)

### Source 2: Plate assignment noise at boundaries (SECONDARY CAUSE, amplifies Source 1)

**Severity: HIGH**

`ResolveWinningPlate()` (Resampling.cpp:500–524) assigns plates based on triangle vertex composition. At boundaries, triangles are typically 2-vs-1 (two vertices from plate A, one from plate B). The "winning plate" depends on which triangle the new point happens to fall in. Adjacent new points can fall in different old triangles with different plate compositions, creating alternating plate assignments along the boundary.

This alternation means:
- After resampling, some new boundary samples are assigned to plate A and some to plate B, in an irregular pattern
- Boundary detection on the new mesh flags a wider zone of samples as boundaries
- This wider boundary zone means more cross-plate triangles in the NEXT resample cycle
- Positive feedback loop: wider boundaries → more cross-plate interpolation → more degradation → wider boundaries

**Affected lines:** Resampling.cpp:500–524 (ResolveWinningPlate), Resampling.cpp:744–746 (plate assignment decision)

### Source 3: DistToFront reset (MODERATE CAUSE)

**Severity: MODERATE**

`InterpolateFromTriangle()` sets `Out.DistToFront = TNumericLimits<float>::Max()` (Resampling.cpp:535). After resampling, ALL DistToFront values are reset to infinity. Then `DetectAndClassifyBoundaries()` calls `ComputeDistanceToFront()`, which recomputes DistToFront via Dijkstra BFS from convergent boundary seed samples.

**Why this matters:**
- DistToFront controls the spatial distribution of subduction uplift (Subduction.cpp:144–145: `if (DistanceKm <= 0.0f || DistanceKm >= PTP::SubductionDist) return;`)
- After resampling, the noisy plate boundaries mean different samples are seeded as convergent boundary samples
- The distance field computed from these noisy seeds differs from the pre-resample distance field
- This changes which samples receive subduction uplift and how much

**However:** The DistToFront reset itself is NOT the degradation mechanism — it's correctly recomputed each time boundaries are detected. The issue is that the *boundary detection inputs* (plate assignments) are noisy after resampling, so the *distance field outputs* are correspondingly noisy. This is an amplifier of Source 2, not an independent source.

**Affected lines:** Resampling.cpp:535, BoundaryDetection.cpp:159–307

### Source 4: Continental erosion (NEGLIGIBLE for this problem)

**Severity: LOW / NOT A FACTOR**

`PlanetConstants.h:19` defines `ContErosion = 3e-2f` (0.03 km/My). Over 120 My:

```
Total erosion = 0.03 × 120 = 3.6 km
```

This is significant but **this constant is defined but never applied in the current code**. Searching through the codebase: `ContErosion` is defined in `PlanetConstants.h:19` but there is no code that actually applies it to sample elevations during the simulation loop. PlateMotion.cpp only rotates positions and directions. Subduction.cpp only applies positive uplift. There is no erosion step.

**Conclusion:** Continental erosion is NOT implemented and cannot be causing degradation.

### Source 5: Subduction eating continents (UNLIKELY)

**Severity: LOW**

`ProcessSubduction()` (Subduction.cpp:93–304) only applies **positive uplift** to overriding plate samples:
- Line 176: `Sample.Elevation = FMath::Min(Sample.Elevation + UpliftKm, PTP::MaxContElevation)` — strictly additive
- The function returns early if `UpliftKm <= 0.0f` (line 171–173)
- `UpliftKm = PTP::SubductionUplift * FD * GV * HZ * DeltaTime` where all factors are ≥ 0

Subduction cannot produce negative uplift. However, there's an indirect path: if cross-plate blending creates continental samples with depressed elevation at convergent boundaries, the `HeightTransfer()` function (Subduction.cpp:48–55) produces a lower transfer factor for lower incoming elevations. This means degraded boundary samples generate less uplift for neighboring overriding-plate samples, creating a subtle feedback. But this is an amplifier, not a root cause.

**Affected lines:** Subduction.cpp:167–176

### Source 6: Discrete field transfer — CrustType misclassification (MODERATE CAUSE)

**Severity: MODERATE**

`InterpolateFromTriangle()` assigns CrustType from the majority-weight vertex (Resampling.cpp:545–553):

```cpp
const int32 MaxWeightVertex = (W1 >= W2 && W1 >= W3) ? 0 : ((W2 >= W3) ? 1 : 2);
const FCrustSample* MajoritySample = (MaxWeightVertex == 0) ? &S1 : (MaxWeightVertex == 1 ? &S2 : &S3);
Out.CrustType = MajoritySample->CrustType;
```

In a cross-plate triangle with 1 continental vertex (weight 0.5) and 2 oceanic vertices (weights 0.25 each), the continental vertex wins because 0.5 > 0.25. But in a triangle with 1 continental vertex (weight 0.3) and 2 oceanic vertices (weights 0.35, 0.35), an oceanic vertex wins. This means continental samples near boundaries can be reclassified as oceanic.

**Why this matters:**
- `RebuildPlateMetadata()` (Resampling.cpp:630–673) determines plate CrustType based on majority sample type (line 671)
- `ClassifyConvergence()` (BoundaryDetection.cpp:114–157) uses plate-side majority CrustType to determine subduction direction
- If enough continental samples at the boundary get misclassified as oceanic, the plate-boundary classification can flip from "oceanic subduction" (correct, where the oceanic plate subducts) to "ocean-ocean subduction" (incorrect, decided by age)
- This can cause the continental plate to be identified as the subducting plate, stopping uplift on the continental side

This is a secondary cascading failure that compounds the primary cross-plate blending problem.

### Source 7: Oceanic age blending (LOW SEVERITY)

**Severity: LOW**

Continental samples have OceanicAge ≈ 0. Oceanic samples accumulate age over time. Cross-plate blending gives continental samples nonzero oceanic age:

```
Out.OceanicAge = W1 × 0.0 + W2 × 0.0 + W3 × OceanicAge_of_oceanic_vertex
```

This is mostly harmless because OceanicAge is only used in two places:
1. `ClassifyConvergence()` (BoundaryDetection.cpp:122–137) — for ocean-ocean subduction, older crust subducts. If a continental sample has nonzero oceanic age, and the boundary gets misclassified as ocean-ocean (per Source 6), the age could affect subduction direction.
2. Not used in elevation calculations directly.

**Conclusion:** Oceanic age blending contributes to cascade failures from Source 6 but is not independently significant.

### Source 8: Thickness blending (NEGLIGIBLE)

**Severity: NEGLIGIBLE**

Continental thickness (35 km) blended with oceanic thickness (7 km) produces intermediate values (~26.6 km for the example in Task 2 Q1). However, `Thickness` is NOT used anywhere in the simulation loop — not in PlateMotion, BoundaryDetection, or Subduction. It's stored but never read for computational purposes. It may affect visualization but not simulation behavior.

**Conclusion:** Thickness blending is cosmetically incorrect but functionally irrelevant to the degradation problem.

### Summary: Degradation Cascade

The degradation is a **cascade** with one primary cause and several amplifying factors:

```
Cross-plate elevation blending (PRIMARY)
  → Continental edge samples lose elevation
  → CrustType misclassification at boundaries (Source 6)
  → Noisy plate assignments (Source 2)
    → Wider boundary zones after resampling
    → More cross-plate triangles in next cycle
    → More blending, more degradation
    → Noisy DistToFront (Source 3)
    → Subduction uplift misdistributed
    → Less uplift at degraded boundaries
    → Continental elevation drops further
```

Each resample cycle widens the affected zone by approximately one mesh edge length (~32 km for 500k samples). After 6 cycles, the affected zone extends ~192 km inward from original plate boundaries.

---

## Task 4: Evaluation of the Proposed Fix

### The Proposed Fix

"Filter interpolation to same-plate vertices only, renormalize weights."

Concretely, this means modifying `InterpolateFromTriangle()` to:
1. Check each vertex's PlateIndex against `AssignedPlateIndex`
2. Zero out weights for vertices from different plates
3. Renormalize remaining weights to sum to 1.0
4. Interpolate only from matching vertices

### Q1: Would this prevent continental degradation?

**Partially, but not completely.** This fix addresses the PRIMARY cause (Source 1: cross-plate elevation blending). It would prevent direct blending of continental and oceanic values. However:

- It does NOT address Source 2 (plate assignment noise). `ResolveWinningPlate()` still produces noisy plate assignments at boundaries. A sample might be assigned to plate B even though it's geometrically closer to plate A's territory. When filtered to plate-B-only vertices, it gets data from a single distant vertex rather than a properly interpolated local value.
- It does NOT address Source 6 (CrustType misclassification) directly, although by preventing elevation blending, it reduces the severity significantly since CrustType will come from same-plate vertices.

**Assessment: This fix would reduce degradation by ~80-90% but may introduce new boundary artifacts.**

### Q2: What happens when ALL THREE vertices belong to a different plate?

This is the critical edge case. If `ResolveWinningPlate()` assigns plate C to a new sample, but the containing triangle has vertices from plates A and B only (no plate C vertices), then:

- All three weights get zeroed out
- Renormalization divides by zero (or near-zero)
- The sample gets undefined/garbage values

**How often does this happen?** `ResolveWinningPlate()` returns one of the three vertex plates (lines 502–523), so the winning plate is ALWAYS one of PlateA, PlateB, or PlateC of the triangle. Therefore, at least one vertex always matches the assigned plate.

**Wait — this is only true for `ResolveWinningPlate()` as currently implemented.** If the winning plate determination were changed to use a different method (e.g., nearest neighbor from a spatial grid), then mismatches could occur. But with the current `ResolveWinningPlate()`, at least one vertex always matches. For a 2-vs-1 triangle (e.g., two plate A, one plate B), the winning plate is A (majority), so at least two vertices match. For a 3-way triangle, the highest-weight vertex wins, so exactly one vertex matches.

**Conclusion:** With the current `ResolveWinningPlate()`, the all-zero-weight scenario cannot occur. But the **single-vertex case** is problematic (see Q4 below).

### Q3: What fraction of triangles at plate boundaries are cross-plate?

For a Delaunay triangulation of 500k samples and ~20 plates:
- Each plate boundary is roughly 1 triangle wide in the mesh
- Boundary zone: approximately 2–4% of all samples are boundary samples (as logged in BoundaryDetection.cpp:591–594)
- Cross-plate triangles: approximately 5–8% of all triangles (each boundary sample participates in ~5-6 triangles, shared with neighboring plates)
- For 500k samples ≈ 1M triangles, cross-plate triangles ≈ 50k–80k

This is NOT a small edge case. It's a significant fraction of the sphere's surface, especially concentrated along all plate boundaries.

### Q4: Could same-plate filtering introduce its own artifacts?

**Yes, significantly.** Consider a 3-way triangle (all three vertices from different plates):

- `ResolveWinningPlate()` picks the highest-weight vertex's plate
- Same-plate filtering zeros out the other two weights
- The surviving vertex gets weight = 1.0 after renormalization
- The new sample copies ALL properties from that single vertex: elevation, thickness, age, directions

This creates **flat plateaus at boundaries** — regions where all samples near a plate edge have identical elevation (from the same nearest same-plate vertex). This is better than cross-plate blending (which creates physically impossible values), but it produces a visible "staircase" artifact along coastlines.

For 2-vs-1 triangles (the more common case):
- If the new sample is assigned to the majority plate (2 matching vertices), weights are renormalized from the two matching vertices. This gives reasonable interpolation within the plate.
- If the new sample is assigned to the minority plate (1 matching vertex), it copies that vertex exactly. This again creates flat zones.

**The flat-zone artifact is less harmful than cross-plate blending** but still produces visible discontinuities, especially over multiple resample cycles where the staircase pattern gets frozen into the data.

### Q5: Is there a better approach?

**Yes. Here are alternatives, ordered from simplest to most sophisticated:**

#### Option A: Same-plate filtering with nearest-neighbor fallback (RECOMMENDED)

Modify `InterpolateFromTriangle()` to:
1. Filter to same-plate vertices, renormalize
2. If only 1 vertex matches → instead of using that single vertex, find the K nearest old samples from the same plate (K=3–6) and perform inverse-distance-weighted interpolation

This handles the common case (2-vs-1 triangles) with simple weight filtering, and handles the edge case (1-match or 3-way triangles) with a local nearest-neighbor fallback that provides smoother interpolation.

**Pros:** Simple to implement, addresses both cross-plate blending and flat-zone artifacts
**Cons:** Nearest-neighbor search for the fallback adds computational cost; need to tune K and distance weighting

#### Option B: Per-plate Delaunay triangulations

Build a separate spherical Delaunay triangulation for each plate's vertices. For each new sample:
1. Determine plate assignment (via nearest-neighbor or `ResolveWinningPlate()`)
2. Find the containing triangle in that plate's Delaunay triangulation
3. Interpolate from that triangle (all vertices guaranteed same-plate)

**Pros:** Eliminates cross-plate blending entirely; provides smooth interpolation within each plate
**Cons:**
- Non-convex plates produce degenerate triangulations — a crescent-shaped plate's Delaunay triangulation includes triangles covering the interior (non-plate) region, potentially overlapping with other plates
- Past iteration #3 tried per-plate convex hull triangulation and found 47% overlap
- Spherical Delaunay of plate subsets produces the convex hull of those points on the sphere, which IS the convex hull — same problem
- Would need constrained Delaunay triangulation to respect plate boundaries, which is significantly more complex on a sphere

#### Option C: Natural neighbor interpolation (Sibson interpolation)

Instead of barycentric interpolation in a triangle, use natural neighbor interpolation based on the Voronoi diagram. Each new point's value is a weighted average of its natural neighbors (the old samples whose Voronoi cells would change if the new point were inserted).

Filter natural neighbors to same-plate only.

**Pros:** Smoother interpolation than barycentric; naturally handles arbitrary point distributions; no flat-zone artifacts
**Cons:** Significantly more complex to implement; requires per-point Voronoi cell computation; high computational cost for 500k points

#### Option D: Radial basis function (RBF) interpolation per plate

For each plate, build a thin-plate-spline or polyharmonic RBF from the plate's sample values. Evaluate at each new sample position.

**Pros:** Very smooth interpolation; handles non-convex plates naturally
**Cons:** Extremely expensive (O(n²) per plate for dense systems, or O(n log n) with compactly supported RBFs); massive implementation effort; overkill for this problem

**Recommendation:** Option A (same-plate filtering with nearest-neighbor fallback) provides the best balance of correctness, simplicity, and performance. It directly fixes the primary degradation cause while avoiding the flat-zone artifact of pure same-plate filtering.

---

## Task 5: What the Paper's Original Implementation Likely Does

### Q1: Point-in-triangle acceleration structure

The paper reports 13.1 seconds for oceanic crust generation (which includes resampling) on 500k points. Given that this was published in 2019, they likely used one of:

1. **STRIPACK** — The paper references Renka's "Algorithm 772: STRIPACK: Delaunay triangulation and Voronoi diagram on the surface of a sphere" (1997). STRIPACK includes a point-location function that walks the triangulation from a starting triangle to find the containing triangle. Walking takes O(√n) per query, so 500k queries × O(√500k) ≈ 500k × 700 = 350M steps. This is plausible for a 13-second runtime.

2. **Grid-based acceleration** — similar to the spatial grid approach in our implementation, but simpler. The paper's authors are computer graphics researchers who would be familiar with spatial hashing.

3. **Simple BVH** — similar to our implementation. Less likely given the STRIPACK reference.

The most probable approach: **STRIPACK walk-based point location** on the old (moved) triangulation. This is algorithmically simple, well-documented, and available as a Fortran library. The authors likely linked against STRIPACK or a C/C++ port.

### Q2: "Triangulation M of the continental and oceanic plates"

The paper describes **one global triangulation M**, not separate per-plate triangulations. The phrase "triangulation M of the continental and oceanic plates" means a triangulation of ALL points (continental and oceanic), which is then partitioned into plate regions.

However, for interpolation during resampling, the phrase "from the plate they intersect" suggests same-plate filtering of the interpolation source data. The implementation likely:

1. Uses the global old (moved) triangulation for point location
2. Determines plate assignment for each new point
3. Interpolates only from same-plate vertices in the found triangle

If no same-plate vertex is available (unlikely but possible), the implementation likely falls back to nearest-neighbor from the same plate.

### Q3: How would per-plate triangulations handle non-convex plates?

If the authors DID use per-plate triangulations (which I consider less likely):

- A spherical Delaunay triangulation of a non-convex plate's vertices produces the **convex hull** of those points on the sphere
- For a crescent-shaped plate, this convex hull includes the interior region that doesn't belong to the plate
- This creates overlapping triangulations between plates

The paper doesn't discuss this problem, which suggests they DON'T use per-plate triangulations. Instead, they likely use the global triangulation with same-plate vertex filtering, which naturally handles non-convex plates because the global triangulation correctly represents the geometry.

### Q4: Is there a simpler approach we're overcomplicating?

**Yes.** The current implementation is architecturally correct in its use of a global BVH for point location on the old triangulation. The only missing piece is **same-plate filtering of the interpolation**. The fix is straightforward:

1. In `InterpolateFromTriangle()`, after computing barycentric weights:
   - Check each vertex's PlateIndex against AssignedPlateIndex
   - Zero out weights for non-matching vertices
   - If sum of remaining weights > 0, renormalize and interpolate
   - If sum = 0 (all vertices from different plates — impossible with current `ResolveWinningPlate()`), fall back to nearest same-plate neighbor

2. For the single-matching-vertex case (only one vertex from the assigned plate):
   - Instead of using that single vertex as a flat copy, optionally do a local nearest-neighbor search within the assigned plate for smoother interpolation
   - This is an optimization, not a requirement — even flat copies are far better than cross-plate blending

The paper's authors almost certainly did something very close to this. Their 13-second runtime for 500k points is consistent with a global-triangulation-point-location + same-plate-filtered-barycentric-interpolation approach.

---

## Diagnosis Summary

### Root Cause

**`InterpolateFromTriangle()` performs unconditional barycentric interpolation from all three triangle vertices regardless of plate membership.** This blends continental and oceanic crust properties at plate boundaries, causing progressive continental degradation over multiple resample cycles.

### Confidence Level

**HIGH.** The analysis is based on direct code tracing, not inference. The math in Task 2 Q1 shows that a single resample cycle can drop a continental sample's elevation by 1.4+ km when it shares a triangle with an oceanic vertex. The cumulative effect over 6 cycles is sufficient to explain the observed degradation pattern.

### Secondary Factors

1. **Plate assignment noise** (Source 2) amplifies the primary cause by widening boundary zones each cycle
2. **CrustType misclassification** (Source 6) can cause downstream subduction logic errors
3. **DistToFront noise** (Source 3) affects uplift distribution after resampling

### What is NOT Causing the Problem

1. Continental erosion — not implemented (ContErosion defined but never applied)
2. Subduction — only applies positive uplift; cannot degrade elevation
3. Thickness blending — cosmetically wrong but not used in computation
4. Oceanic age blending — minor, only affects edge-case subduction classification
5. Gap sample generation (BuildGapSample) — correctly implemented for divergent boundaries

### Recommended Fix

**Option A: Same-plate vertex filtering with nearest-neighbor fallback**, as described in Task 4 Q5.

Implementation sketch for `InterpolateFromTriangle()`:

```cpp
// 1. Identify matching vertices
float FilteredWeights[3] = { 0.0f, 0.0f, 0.0f };
int32 MatchCount = 0;
const int32 Plates[3] = { Tri.PlateA, Tri.PlateB, Tri.PlateC };
const float RawWeights[3] = { Weights.X, Weights.Y, Weights.Z };

for (int32 i = 0; i < 3; ++i)
{
    if (Plates[i] == AssignedPlateIndex)
    {
        FilteredWeights[i] = RawWeights[i];
        ++MatchCount;
    }
}

// 2. Renormalize
const float WeightSum = FilteredWeights[0] + FilteredWeights[1] + FilteredWeights[2];
if (WeightSum > 1.0e-8f)
{
    for (int32 i = 0; i < 3; ++i)
        FilteredWeights[i] /= WeightSum;
}
else
{
    // Fallback: nearest same-plate neighbor (should not happen with current ResolveWinningPlate)
    // ... nearest-neighbor search ...
}

// 3. Interpolate with filtered weights
Out.Elevation = FilteredWeights[0] * S1.Elevation
              + FilteredWeights[1] * S2.Elevation
              + FilteredWeights[2] * S3.Elevation;
// ... same for other continuous fields ...

// 4. Discrete fields from highest-weight SAME-PLATE vertex
int32 MajorityIdx = 0;
for (int32 i = 1; i < 3; ++i)
    if (FilteredWeights[i] > FilteredWeights[MajorityIdx])
        MajorityIdx = i;
// Use MajorityIdx for CrustType, OrogenyType
```

This preserves the existing architecture (global BVH point location, barycentric interpolation) while eliminating cross-plate data contamination. It's the minimal, targeted fix that addresses the root cause.

---

## Appendix: File Reference

| File | Key Lines | Relevance |
|------|-----------|-----------|
| `Resampling.cpp` | 526–564 | `InterpolateFromTriangle()` — **THE BUG** |
| `Resampling.cpp` | 500–524 | `ResolveWinningPlate()` — plate assignment |
| `Resampling.cpp` | 548 | Elevation blending without plate filtering |
| `Resampling.cpp` | 545–546 | Majority-vertex CrustType selection |
| `Resampling.cpp` | 535 | DistToFront reset to Max |
| `Resampling.cpp` | 566–628 | `BuildGapSample()` — divergent gap handling (correct) |
| `Resampling.cpp` | 676–790 | `GlobalResample()` — main pipeline |
| `Resampling.cpp` | 733–736 | Gap triangle detection criteria |
| `BoundaryDetection.cpp` | 329–343 | Boundary flag detection (sensitive to noisy plate assignments) |
| `BoundaryDetection.cpp` | 159–307 | `ComputeDistanceToFront()` — Dijkstra BFS |
| `Subduction.cpp` | 167–176 | Uplift application (positive only, not a cause) |
| `PlanetConstants.h` | 19 | `ContErosion = 3e-2f` — defined but never used |
| `CrustSample.h` | 7–26 | Per-sample data structure |
