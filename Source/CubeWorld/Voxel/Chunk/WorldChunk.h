#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelBiome.h"
#include "WorldChunk.generated.h"

class UVoxelObject;

/**
 * Represents a single chunk of voxel terrain.
 * Generates a grid of cube columns using procedural mesh geometry.
 * Biomes are determined via Worley noise; each biome has its own height profile and color.
 */
UCLASS()
class CUBEWORLD_API AWorldChunk : public AActor
{
	GENERATED_BODY()

public:
	AWorldChunk();

	/**
	 * Generate the chunk mesh for the given 3D chunk coordinate.
	 * Uses Worley noise to pick a biome per column, then biome-specific Perlin noise for height.
	 * Heights and colors are smoothly blended at biome borders.
	 * Each chunk covers a fixed vertical slice [ZLayer * ChunkHeight, (ZLayer+1) * ChunkHeight).
	 *
	 * @param InChunkCoord     Horizontal grid coordinate of this chunk
	 * @param InZLayer         Vertical layer index (0 = ground level, 1 = above, etc.)
	 * @param InChunkSize      Number of columns per chunk side
	 * @param InChunkHeight    Number of voxel rows per vertical chunk layer
	 * @param InVoxelSize      World-space size of one cube
	 * @param InBiomeCellSize  World-space size of a Worley cell (controls biome scale)
	 * @param InSeed           World seed
	 * @param InBiomes         Per-biome noise and visual parameters
	 * @param InBlendWidth     Biome blend width (fraction of cell size, 0-1)
	 * @param InMaterial       Material to apply
	 */
	void GenerateChunk(
		FIntPoint InChunkCoord,
		int32 InZLayer,
		int32 InChunkSize,
		int32 InChunkHeight,
		float InVoxelSize,
		float InBiomeCellSize,
		float InSeed,
		const TArray<FVoxelBiomeParams>& InBiomes,
		float InBlendWidth,
		UMaterialInterface* InMaterial);

	/** Returns the chunk coordinate this chunk was generated for. */
	FIntPoint GetChunkCoord() const { return ChunkCoord; }

	/** Returns the vertical layer of this chunk. */
	int32 GetZLayer() const { return ZLayer; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	UVoxelObject* VoxelObject;

private:
	FIntPoint ChunkCoord;
	int32 ZLayer = 0;
};
