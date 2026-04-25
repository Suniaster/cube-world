#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "../VoxelObject.h"
#include "../VoxelTypes.h"
#include "VoxelBiome.h"
#include "VoxelTerrainNoise.h"
#include "../Features/VoxelFeatureGenerator.h"

/** Result of a background chunk generation task. */
struct FChunkGenerationResult
{
	FIntPoint ChunkCoord;
	int32 ZLayer;
	int32 LODLevel;
	/** Full-resolution max terrain height (voxel rows) for this XY column. Only valid on ZLayer == 0. */
	int32 ColumnMaxHeight;
	FVoxelMeshData HeightmapData;
	TMap<uint8, FVoxelMeshData> BlockMeshes;
	TArray<FFeaturePlacement> FeaturePlacements;
	bool bHasAnyBlocks;
	bool bSuccess;
	bool bIsHeightmap;

	FChunkGenerationResult()
		: ChunkCoord(0, 0), ZLayer(0), LODLevel(0), ColumnMaxHeight(0), bHasAnyBlocks(false), bSuccess(false), bIsHeightmap(false)
	{}
};

/** 
 * Background task that handles noise sampling, voxel grid population, 
 * and greedy meshing for a single chunk.
 */
class FChunkGenerationTask : public FNonAbandonableTask
{
public:
	FChunkGenerationTask(
		FIntPoint InChunkCoord,
		int32 InChunkSize,
		int32 InChunkHeight,
		float InVoxelSize,
		int32 InLODLevel,
		float InBiomeCellSize,
		float InSeed,
		const TArray<FVoxelBiomeParams>& InBiomes,
		float InBlendWidth,
		TQueue<FChunkGenerationResult, EQueueMode::Mpsc>* InQueue,
		int32 InWaterLevel,
		int32 InMaxHeightHint,
		int32 InHeightmapResolution,
		int32 InMaxTreeLOD,
		const TArray<TSharedPtr<const IVoxelFeatureGenerator, ESPMode::ThreadSafe>>& InFeatures)
		: ChunkCoord(InChunkCoord), ChunkSize(InChunkSize), ChunkHeight(InChunkHeight), VoxelSize(InVoxelSize),
		  LODLevel(InLODLevel), BiomeCellSize(InBiomeCellSize), Seed(InSeed), Biomes(InBiomes), BlendWidth(InBlendWidth),
		  ResultQueue(InQueue), WaterLevel(InWaterLevel), MaxHeightHint(InMaxHeightHint), HeightmapResolution(InHeightmapResolution),
		  MaxTreeLOD(InMaxTreeLOD), Features(InFeatures)
	{}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FChunkGenerationTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	void DoWork();

private:
	FIntPoint ChunkCoord;
	int32 ChunkSize;
	int32 ChunkHeight;
	float VoxelSize;
	int32 LODLevel;
	float BiomeCellSize;
	float Seed;
	TArray<FVoxelBiomeParams> Biomes;
	float BlendWidth;

	TQueue<FChunkGenerationResult, EQueueMode::Mpsc>* ResultQueue;
	int32 WaterLevel;
	/** Cached full-res max height from a previous generation of this column. 0 = unknown. */
	int32 MaxHeightHint;
	/** Number of cells per side for the LOD 3+ heightmap mesh (e.g. 4 = 4x4 = 32 tris). */
	int32 HeightmapResolution;
	int32 MaxTreeLOD;

	TArray<TSharedPtr<const IVoxelFeatureGenerator, ESPMode::ThreadSafe>> Features;

	// Helper functions for DoWork()
	bool GenerateLOD3Heightmap(float ChunkWorldX, float ChunkWorldY, const FVoxelTerrainNoise::FCachedWorleyPoints& CachedPoints);
	void GenerateBilinearInterpolation(
		float ChunkWorldX, float ChunkWorldY, int32 EffectiveChunkSize, float EffectiveVoxelSize,
		const FVoxelTerrainNoise::FCachedWorleyPoints& CachedPoints,
		TArray<int32>& OutHeightMap, TArray<uint8>& OutBlockTypeMap, TArray<FColor>& OutColorMap, int32& OutAbsoluteMaxHeight);
	void GenerateVoxelLayers(
		const TArray<int32>& HeightMap, const TArray<uint8>& BlockTypeMap, const TArray<FColor>& ColorMap,
		int32 AbsoluteMaxHeight, int32 EffectiveChunkSize, int32 EffectiveChunkHeight, float EffectiveVoxelSize, int32 LODScale);
};
