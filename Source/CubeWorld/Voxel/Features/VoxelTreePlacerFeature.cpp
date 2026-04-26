#include "VoxelTreePlacerFeature.h"

FVoxelTreePlacerFeature::FVoxelTreePlacerFeature(float InCellSize, float InMaxRadius, int32 InArchetypeCount)
	: CellSize(InCellSize)
	, MaxRadius(InMaxRadius)
	, ArchetypeCount(InArchetypeCount)
{
}

void FVoxelTreePlacerFeature::ComputePlacements(const FChunkPlacementContext& Context, TArray<FFeaturePlacement>& OutPlacements) const
{
	if (ArchetypeCount <= 0 || CellSize <= 0.f) return;

	const float ChunkMinX = Context.ChunkWorldX;
	const float ChunkMinY = Context.ChunkWorldY;
	const float ChunkMaxX = ChunkMinX + Context.ChunkSize * Context.VoxelSize;
	const float ChunkMaxY = ChunkMinY + Context.ChunkSize * Context.VoxelSize;

	const int32 CellMinX = FMath::FloorToInt32((ChunkMinX - MaxRadius) / CellSize);
	const int32 CellMaxX = FMath::FloorToInt32((ChunkMaxX + MaxRadius) / CellSize);
	const int32 CellMinY = FMath::FloorToInt32((ChunkMinY - MaxRadius) / CellSize);
	const int32 CellMaxY = FMath::FloorToInt32((ChunkMaxY + MaxRadius) / CellSize);

	for (int32 cX = CellMinX; cX <= CellMaxX; ++cX)
	{
		for (int32 cY = CellMinY; cY <= CellMaxY; ++cY)
		{
			const int32 CellSeed = (cX * 73856093) ^ (cY * 19349663) ^ FMath::FloorToInt32(Context.Seed * 10000.f);
			FRandomStream CellRandom(CellSeed);

			const float CellWorldX = cX * CellSize + (CellSize * 0.5f);
			const float CellWorldY = cY * CellSize + (CellSize * 0.5f);

			const float OffsetX = CellRandom.FRandRange(-CellSize * 0.35f, CellSize * 0.35f);
			const float OffsetY = CellRandom.FRandRange(-CellSize * 0.35f, CellSize * 0.35f);
			// Snap to world voxel center for perfect grid alignment in a cube world.
			const float WorldX = (FMath::FloorToFloat((CellWorldX + OffsetX) / Context.VoxelSize) + 0.5f) * Context.VoxelSize;
			const float WorldY = (FMath::FloorToFloat((CellWorldY + OffsetY) / Context.VoxelSize) + 0.5f) * Context.VoxelSize;

			// Skip if not within this chunk's footprint
			if (WorldX < ChunkMinX || WorldX >= ChunkMaxX || WorldY < ChunkMinY || WorldY >= ChunkMaxY)
			{
				continue;
			}

			// Look up the actual voxel height from the bilinearly-interpolated heightmap
			// used to build the voxel grid, so tree Z matches the rendered terrain exactly.
			const float LocalX = (WorldX - Context.ChunkWorldX) / Context.EffectiveVoxelSize;
			const float LocalY = (WorldY - Context.ChunkWorldY) / Context.EffectiveVoxelSize;
			const int32 IX = FMath::Clamp(FMath::FloorToInt32(LocalX), 0, Context.EffectiveChunkSize - 1);
			const int32 IY = FMath::Clamp(FMath::FloorToInt32(LocalY), 0, Context.EffectiveChunkSize - 1);
			const int32 ColIdx = (IX + 1) * Context.HeightMapWidth + (IY + 1);
			const int32 H = Context.HeightMap[ColIdx];
			const uint8 PrimaryBiome = Context.BiomeMap[ColIdx];

			if (H <= Context.WaterLevel) continue;

			const int32 BIdx = FMath::Clamp<int32>(PrimaryBiome - 1, 0, Context.Biomes.Num() - 1);
			const float Density = Context.Biomes[BIdx].TreeDensity;

			if (Density > 0.0f && CellRandom.FRand() <= Density)
			{
				const int32 ArchetypeIdx = CellRandom.RandHelper(ArchetypeCount);
				const float WorldZ = static_cast<float>(H) * Context.VoxelSize;

				FFeaturePlacement& Placement = OutPlacements.AddDefaulted_GetRef();
				Placement.WorldPosition = FVector(WorldX, WorldY, WorldZ);
				Placement.ArchetypeIndex = ArchetypeIdx;
			}
		}
	}
}
