#pragma once

#include "CoreMinimal.h"
#include "../VoxelTypes.h"
#include "../Chunk/VoxelBiome.h"

/** 
 * Describes a single request to place a feature archetype at a world position.
 * Produced on the async thread and consumed on the game thread.
 */
struct FFeaturePlacement
{
	/** World-space base position of the feature (ground level). */
	FVector WorldPosition;

	/** Index into the world-manager's pre-built archetype array. */
	int32 ArchetypeIndex;
};

/** 
 * Read-only positional/biome context passed to feature generators.
 * Intentionally has NO reference to the voxel grid — features must NOT write voxels.
 * HeightMap and BiomeMap are the bilinearly-interpolated column data already computed
 * for voxel generation, indexed as (X+1)*HeightMapWidth + (Y+1) for X,Y in [0, ChunkSize-1].
 */
struct FChunkPlacementContext
{
	FIntPoint ChunkCoord;
	/** Original full-resolution chunk size. */
	int32 ChunkSize;
	/** Original base voxel size. */
	float VoxelSize;

	/** Current LOD-scaled chunk size (e.g. ChunkSize / 2 for LOD 1). */
	int32 EffectiveChunkSize;
	/** Current LOD-scaled voxel size (e.g. VoxelSize * 2 for LOD 1). */
	float EffectiveVoxelSize;

	float ChunkWorldX;
	float ChunkWorldY;

	float BiomeCellSize;
	float Seed;
	const TArray<FVoxelBiomeParams>& Biomes;
	float BlendWidth;
	int32 WaterLevel;

	/** Per-column terrain height (voxel rows), same data used to build the voxel grid. */
	const TArray<int32>& HeightMap;
	/** Per-column primary biome index, matches HeightMap layout. */
	const TArray<uint8>& BiomeMap;
	/** Width of HeightMap/BiomeMap arrays: EffectiveChunkSize + 2. */
	int32 HeightMapWidth;
};

/** 
 * Base interface for anything that adds procedural features
 * (trees, houses, dungeons) to the world during chunk generation.
 * Implementations compute PLACEMENT POSITIONS ONLY on the async thread.
 * The game thread is responsible for actually rendering the features (e.g. via HISM or Actors).
 */
class IVoxelFeatureGenerator
{
public:
	virtual ~IVoxelFeatureGenerator() = default;

	/**
	 * Called on the async thread to compute where features should be placed.
	 * MUST be fully thread-safe. Must NOT access any UObject or game-thread state.
	 * Append your results into OutPlacements.
	 */
	virtual void ComputePlacements(const FChunkPlacementContext& Context, TArray<FFeaturePlacement>& OutPlacements) const = 0;
};
