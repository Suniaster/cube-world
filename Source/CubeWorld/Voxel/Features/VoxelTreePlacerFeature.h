#pragma once

#include "CoreMinimal.h"
#include "VoxelFeatureGenerator.h"

/**
 * Deterministically places tree archetypes in a grid-based layout.
 * Does NOT touch the voxel grid — outputs FFeaturePlacement positions only.
 * The game thread will manage the actual tree instances in HISM components.
 */
class FVoxelTreePlacerFeature : public IVoxelFeatureGenerator
{
public:
	FVoxelTreePlacerFeature(float InCellSize, float InMaxRadius, int32 InArchetypeCount);

	virtual void ComputePlacements(const FChunkPlacementContext& Context, TArray<FFeaturePlacement>& OutPlacements) const override;

private:
	float CellSize;
	float MaxRadius;
	int32 ArchetypeCount;
};
