#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldChunk.generated.h"

class UProceduralMeshComponent;

/**
 * Represents a single chunk of voxel terrain.
 * Generates a grid of cube columns using procedural mesh geometry.
 */
UCLASS()
class CUBEWORLD_API AWorldChunk : public AActor
{
	GENERATED_BODY()

public:
	AWorldChunk();

	/**
	 * Generate the chunk mesh for the given chunk coordinate.
	 * Each chunk spans ChunkSize x ChunkSize columns of cubes.
	 *
	 * @param InChunkCoord   Grid coordinate of this chunk (not world space)
	 * @param InChunkSize    Number of columns per chunk side
	 * @param InVoxelSize    World-space size of one cube
	 * @param InFrequency    Noise frequency
	 * @param InAmplitude    Max height in voxel columns
	 * @param InOctaves      Noise octave count
	 * @param InPersistence  Noise persistence
	 * @param InLacunarity   Noise lacunarity
	 * @param InSeed         World seed
	 */
	void GenerateChunk(
		FIntPoint InChunkCoord,
		int32 InChunkSize,
		float InVoxelSize,
		float InFrequency,
		float InAmplitude,
		int32 InOctaves,
		float InPersistence,
		float InLacunarity,
		float InSeed);

	/** Returns the chunk coordinate this chunk was generated for. */
	FIntPoint GetChunkCoord() const { return ChunkCoord; }

protected:
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* TerrainMesh;

private:
	FIntPoint ChunkCoord;

	/** Add the 6 faces of a single cube to the mesh arrays. Skips faces adjacent to solid neighbors. */
	void AddCube(
		FVector Position,
		float Size,
		const TArray<TArray<int32>>& HeightMap,
		int32 LocalX, int32 LocalY, int32 Z,
		int32 ChunkSizeVal,
		FColor CubeColor,
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FColor>& VertexColors);

	/** Returns a color using smooth noise-based variation across the terrain. */
	static FColor GetVoxelColor(float WorldX, float WorldY);
};
