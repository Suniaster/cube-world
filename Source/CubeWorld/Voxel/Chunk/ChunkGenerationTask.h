#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "../VoxelObject.h"
#include "../VoxelTypes.h"
#include "VoxelBiome.h"

/** Result of a background chunk generation task. */
struct FChunkGenerationResult
{
	FIntPoint ChunkCoord;
	int32 ZLayer;
	int32 LODLevel;
	FVoxelMeshData MeshData;
	bool bHasAnyBlocks;
	bool bSuccess;

	FChunkGenerationResult()
		: ChunkCoord(0, 0), ZLayer(0), LODLevel(0), bHasAnyBlocks(false), bSuccess(false)
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
		TArray<FVoxelBiomeParams> InBiomes,
		float InBlendWidth,
		TQueue<FChunkGenerationResult, EQueueMode::Mpsc>* InResultQueue)
		: ChunkCoord(InChunkCoord)
		, ChunkSize(InChunkSize)
		, ChunkHeight(InChunkHeight)
		, VoxelSize(InVoxelSize)
		, LODLevel(InLODLevel)
		, BiomeCellSize(InBiomeCellSize)
		, Seed(InSeed)
		, Biomes(MoveTemp(InBiomes))
		, BlendWidth(InBlendWidth)
		, ResultQueue(InResultQueue)
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
};
