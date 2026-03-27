#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelBiome.h"
#include "../VoxelObject.h"
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


	/** Applies pre-generated mesh data to this chunk (must run on game thread). */
	void ApplyGeneratedMesh(
		const FIntVector& InKey,
		const FVoxelMeshData& InMeshData,
		UMaterialInterface* InMaterial,
		int32 InLODLevel = 0);

	/** Returns the chunk coordinate this chunk was generated for. */
	FIntPoint GetChunkCoord() const { return ChunkCoord; }

	/** Returns the vertical layer of this chunk. */
	int32 GetZLayer() const { return ZLayer; }

	/** Returns the LOD level of this chunk (0 = full detail). */
	int32 GetLODLevel() const { return LODLevel; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	UVoxelObject* VoxelObject;

private:
	FIntPoint ChunkCoord;
	int32 ZLayer = 0;
	int32 LODLevel = 0;
};
