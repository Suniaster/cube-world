# Lessons Learned

## 1. UE ProceduralMeshComponent: Triangle Winding Order (2026-03-22)
**Problem**: All terrain top faces were invisible (back-face culled), making the ground appear missing while side faces showed as walls.
**Cause**: Used counter-clockwise (CCW) winding order, but UE/DirectX requires **clockwise (CW)** winding for front-facing triangles.
**Fix**: Reversed triangle indices from `(0,1,2)(0,2,3)` to `(0,2,1)(0,3,2)` for each quad face.
**Rule**: When generating ProceduralMeshComponent geometry in UE, always use **clockwise winding** when viewed from the front face.
