# Refactor: Multi-Word Bitmask Greedy Mesher (Option A)

## Goal

Replace the single `uint64` bitmask per column in the binary greedy mesher with a fixed-width multi-word bitmask, removing the hard 64-voxel-per-axis cap and allowing chunk dimensions (particularly `ChunkHeight`) to exceed 64 voxels — up to 128 or 256.

---

## Background & Current Implementation

File: `Source/CubeWorld/Voxel/VoxelObject.cpp`

The mesher works in four stages:

### Stage 1 — Column Masks
For every voxel (X, Y, Z), a bit is set in one `uint64` per axis-column:
```cpp
TArray<uint64> SolidAxisCols[3];  // [axis][column_index]
TArray<uint64> WaterAxisCols[3];

// AXIS_Z column: X + Y * SizeX, bit = Z
Cols[AXIS_Z][X + Y * GridSize.X] |= (1ULL << Z);
// AXIS_X column: Y + Z * SizeY, bit = X
Cols[AXIS_X][Y + Z * GridSize.Y] |= (1ULL << X);
// AXIS_Y column: Z + X * SizeZ, bit = Y
Cols[AXIS_Y][Z + X * GridSize.Z] |= (1ULL << Y);
```
**Hard limit:** `1ULL << N` is only valid for N < 64.

### Stage 2 — Face Culling (bit-shift neighbour comparison)
```cpp
const uint64 SNegMask = SolidCol << 1 | SNegBit;
const uint64 SPosMask = SolidCol >> 1 | (SPosBit << (AxisDim - 1));
const uint64 AnyNegMask = AnyCol << 1 | (SNegBit | WNegBit);
const uint64 AnyPosMask = AnyCol >> 1 | ((SPosBit | WPosBit) << (AxisDim - 1));
ColFaceMasks[Axis][0][i] = (SolidCol & ~SNegMask) | (WaterCol & ~AnyNegMask);
ColFaceMasks[Axis][1][i] = (SolidCol & ~SPosMask) | (WaterCol & ~AnyPosMask);
```
**Hard limit:** All shifts assume single `uint64`.

### Stage 3 — Plane Grouping
Each set bit in a face mask identifies a visible voxel. Its position is extracted via `CountTrailingZeros64`, then placed into a 2D plane (one `uint64` row per `V1` position, bit = `V2`):
```cpp
int32 V3 = FMath::CountTrailingZeros64(Col);
Col &= Col - 1;
Plane[V1] |= (1ULL << V2);
```
**Hard limit:** `CountTrailingZeros64` and the bit index are both capped at 63.

### Stage 4 — `GreedyMeshBinaryPlane`
```cpp
static TArray<FGreedyQuad> GreedyMeshBinaryPlane(TArray<uint64>& Data, int32 PlaneHeight)
```
Scans each row `uint64` for set bits, counts consecutive runs (height H), grows horizontally (width W), clears used bits. Explicitly breaks at `Y + H >= 64`.

---

## What Needs to Change

### Step 1 — Define a `FBitMask` struct

Create a small fixed-width bitmask helper, ideally in `VoxelTypes.h` or a new `VoxelBitMask.h`. Support 2 words (128 bits) as the default target, templated on `N` words if you want flexibility:

```cpp
template<int32 NumWords>
struct TBitMask
{
    uint64 Words[NumWords] = {};

    void SetBit(int32 Bit);
    bool GetBit(int32 Bit) const;
    void ClearBit(int32 Bit);

    TBitMask operator|(const TBitMask& O) const;
    TBitMask operator&(const TBitMask& O) const;
    TBitMask operator~() const;

    // Shift left by 1 (with inter-word carry)
    TBitMask ShiftLeft1() const;
    // Shift right by 1 (with inter-word carry)
    TBitMask ShiftRight1() const;

    // OR a single bit into word 0 (for SNegBit boundary injection)
    TBitMask OrLowBit(uint64 Bit) const;
    // OR a single bit at position (MaxBit - 1) (for SPosBit boundary injection)
    TBitMask OrHighBit(uint64 Bit, int32 AxisDim) const;

    // CountTrailingZeros: index of the first set bit across all words (-1 if zero)
    int32 CTZ() const;
    // Returns true if all words are zero
    bool IsZero() const;
    // Clear lowest set bit (equivalent to Mask &= Mask - 1)
    TBitMask ClearLowestBit() const;

    // Extract the lowest N bits as a mask (for HAsMask in GreedyMeshBinaryPlane)
    TBitMask MakeLowMask(int32 H) const;
    // Shift left by arbitrary amount (for placing masks at offset Y)
    TBitMask ShiftLeftBy(int32 Amount) const;
    // ShiftRightBy for extracting sub-range
    TBitMask ShiftRightBy(int32 Amount) const;

    // AND a mask starting at bit Y of length H (get/compare sub-range)
    TBitMask GetBitsAt(int32 Y, int32 H) const;
};

using FBitMask128 = TBitMask<2>;
using FBitMask256 = TBitMask<4>;
```

Key implementation notes:
- `ShiftLeft1`: `Words[i+1] |= Words[i] >> 63; Words[i] <<= 1;` (iterate low to high)
- `ShiftRight1`: reverse direction, carry `Words[i] & 1` into `Words[i-1] << 63`
- `CTZ`: iterate words, return `WordIdx * 64 + CountTrailingZeros64(Words[WordIdx])` for first non-zero word
- `ClearLowestBit`: subtract 1 with borrow propagation across words, then AND

### Step 2 — Replace `uint64` arrays with `TArray<FBitMask128>`

In `GenerateMeshData`:
```cpp
// Before:
TArray<uint64> SolidAxisCols[3];
TArray<uint64> WaterAxisCols[3];
TArray<uint64> ColFaceMasks[3][2];

// After:
TArray<FBitMask128> SolidAxisCols[3];
TArray<FBitMask128> WaterAxisCols[3];
TArray<FBitMask128> ColFaceMasks[3][2];
```

### Step 3 — Update Stage 1 (Column Mask Population)

```cpp
// Before:
Cols[AXIS_Z][X + Y * GridSize.X] |= (1ULL << Z);

// After:
Cols[AXIS_Z][X + Y * GridSize.X].SetBit(Z);
```

### Step 4 — Update Stage 2 (Face Culling)

Replace all single-word shift+mask operations with the multi-word equivalents:
```cpp
// Before:
const uint64 SNegMask = SolidCol << 1 | SNegBit;
const uint64 SPosMask = SolidCol >> 1 | (SPosBit << (AxisDim - 1));

// After:
const FBitMask128 SNegMask = SolidCol.ShiftLeft1().OrLowBit(SNegBit);
const FBitMask128 SPosMask = SolidCol.ShiftRight1().OrHighBit(SPosBit, AxisDim);
```

The neighbor boundary bits (`SNegBit`, `SPosBit`, `WNegBit`, `WPosBit`) remain scalar `uint64` (they represent a single boundary voxel — just one bit injected at position 0 or AxisDim-1). The `OrLowBit`/`OrHighBit` helpers handle injecting them into the correct word.

The `FVoxelNeighborMasks` struct stores neighbor data as `TArray<uint64>` bitsets (one bit per column). That encoding is about *which columns* have solid/water neighbors, not about the height axis — it does NOT need to change.

### Step 5 — Update Stage 3 (Plane Grouping)

Replace the CTZ + bit-clear loop and plane insertion:
```cpp
// Before:
TMap<int32, TArray<uint64>> PlaneGroups[6][2];
...
int32 V3 = FMath::CountTrailingZeros64(Col);
Col &= Col - 1;
Plane[V1] |= (1ULL << V2);

// After:
TMap<int32, TArray<FBitMask128>> PlaneGroups[6][2];
...
int32 V3 = Col.CTZ();
Col = Col.ClearLowestBit();
Plane[V1].SetBit(V2);
```

Note: V3 is now an int that can exceed 63. V2 similarly can exceed 63 when `PlaneHeight > 64`, so `SetBit(V2)` must target word `V2 / 64`, bit `V2 % 64`.

### Step 6 — Update `GreedyMeshBinaryPlane`

Signature change:
```cpp
// Before:
static TArray<FGreedyQuad> GreedyMeshBinaryPlane(TArray<uint64>& Data, int32 PlaneHeight)

// After:
static TArray<FGreedyQuad> GreedyMeshBinaryPlane(TArray<FBitMask128>& Data, int32 PlaneHeight)
```

Internal changes:
- Replace `Data[Row] >> Y` with `Data[Row].ShiftRightBy(Y)`
- Replace CTZ with `.CTZ()`
- Replace the `while (TempBits & 1)` run-length counter with a version using `GetBit(0)` on the shifted mask
- Remove the `if (Y + H >= 64) break;` line — this is the bug being fixed
- Replace `HAsMask = (H == 64) ? ~0ULL : ((1ULL << H) - 1)` with `FBitMask128::MakeLowMask(H)`
- Replace `Mask = HAsMask << Y` with `HAsMask.ShiftLeftBy(Y)`
- Replace `(Data[Row + W] >> Y) & HAsMask` with `Data[Row + W].ShiftRightBy(Y) & HAsMask`
- Replace `Data[Row + W] &= ~Mask` with the multi-word AND-NOT equivalent

---

## Neighbor Mask Compatibility (`FVoxelNeighborMasks`)

`FVoxelNeighborMasks::NeighborBits[Type][Axis][Dir]` is a `TArray<uint64>` where each bit represents one *column* (not one voxel height). The lookup:
```cpp
const int32 MaskIdx = i / 64;
const uint64 MaskBit = 1ULL << (i % 64);
SNegBit = (NeighborBits[0][Axis][0][MaskIdx] & MaskBit) ? 1ULL : 0ULL;
```
This uses column index `i` (which can be up to `SizeX * SizeY`, `SizeY * SizeZ`, etc.). It is unrelated to voxel height. **No changes needed here.**

---

## Chunk Configuration After This Change

With `FBitMask128` (2 × uint64 = 128 bits), the new hard limit per axis is **128 voxels**.

- Set `ChunkHeight` up to 128 in `AChunkWorldManager`.
- `MaxZLayersLimit` and `ActiveZLayers` clamp can remain as-is since tall chunks reduce the number of Z-layers needed.
- `ChunkSize` (X and Y) is also limited to 128 now (bits for X and Y axes also use the same bitmask type). If only height needs to grow, you can make the type per-axis or only use `FBitMask128` for AXIS_Z columns and keep `uint64` for AXIS_X/AXIS_Y — but a uniform type is simpler.

For 256-height, switch `using FBitMask128 = TBitMask<2>` to `TBitMask<4>` throughout.

---

## Files to Modify

| File | Changes |
|------|---------|
| `Source/CubeWorld/Voxel/VoxelTypes.h` | Add `TBitMask<N>` struct and `FBitMask128` alias |
| `Source/CubeWorld/Voxel/VoxelObject.cpp` | Replace all `uint64` column/face/plane arrays and operations with `FBitMask128` equivalents |
| `Source/CubeWorld/Voxel/VoxelObject.h` | No changes expected |
| `Source/CubeWorld/World/ChunkWorldManager.h` | Optionally raise default `ChunkHeight` (e.g. to 128) after mesher supports it |

---

## Acceptance Criteria

1. Compile cleanly with no `uint64` shift-by->=64 undefined behaviour.
2. `ChunkHeight` can be set to 128 in the editor without visual artefacts (seams, missing faces, wrong face culling at chunk boundaries).
3. Greedy meshing produces identical output to the old code for any chunk with all dimensions ≤ 64.
4. No regression in water face culling (water/solid seam boundaries are still correct).
5. Performance: meshing a 64×64×128 chunk should be no slower than 2× a 64×64×64 chunk (the work scales linearly with voxel count, the bitmask overhead is small).
