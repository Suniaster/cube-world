#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelBiome.h"
#include "ChunkWorldManager.generated.h"

class AWorldChunk;

/**
 * Manages dynamic loading/unloading of terrain chunks around the player.
 * Place one of these in your level — terrain is generated automatically at runtime.
 */
UCLASS()
class CUBEWORLD_API AChunkWorldManager : public AActor
{
	GENERATED_BODY()

public:
	AChunkWorldManager();

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

	// ── Terrain shape ───────────────────────────────────────────────────

	/** Number of voxel columns per chunk side (e.g. 16 = 16x16 grid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	int32 ChunkSize = 32;

	/** World-space size of one cube in UU. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	float VoxelSize = 100.0f;

	/** How many chunks around the player to keep loaded (Manhattan radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	int32 RenderDistance = 5;

	// ── Noise parameters ────────────────────────────────────────────────

	/** World-space size of a Worley cell. Controls how large biome regions are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Biomes")
	float BiomeCellSize = 100000.0f;

	/** Per-biome noise and visual parameters. Index 0 = SnowMountains, 1 = ForestPlains, 2 = Desert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Biomes")
	TArray<FVoxelBiomeParams> Biomes;

	/** How wide the transition zone between biomes is, as a fraction of cell size (0 = hard edge, 0.5 = very wide). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Biomes", meta = (ClampMin = "0", ClampMax = "1"))
	float BiomeBlendWidth = 0.35f;

	/** World seed – different seeds produce different landscapes and biome layouts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	float Seed = 0.0f;

	/** Maximum number of chunks to load in a single frame. Prevents hitches/crashes at high distances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	int32 MaxChunksPerFrame = 10;

	/** The material to apply to all chunks. If null, a default lit vertex color material is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Material")
	UMaterialInterface* TerrainMaterial;

private:
	/** Currently loaded chunks, keyed by chunk coordinate. */
	TMap<FIntPoint, AWorldChunk*> LoadedChunks;

	/** Last known player chunk coord – avoids redundant updates. */
	FIntPoint LastPlayerChunk;
	bool bHasLastPlayerChunk = false;

	/** Queue of chunk coordinates waiting to be loaded. */
	TArray<FIntPoint> ChunkLoadQueue;

	/** Cached material created at runtime if TerrainMaterial is null. */
	UPROPERTY()
	UMaterialInterface* CachedRuntimeMaterial;

	/** Ensures the terrain material is loaded or created. */
	void EnsureMaterial();

	/** Convert a world position to a chunk coordinate. */
	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;

	/** Load a chunk at the given coordinate (no-op if already loaded). */
	void LoadChunk(FIntPoint Coord);

	/** Unload (destroy) a chunk at the given coordinate. */
	void UnloadChunk(FIntPoint Coord);

	/** Full update: load nearby chunks, unload distant ones. */
	void UpdateChunksAroundPlayer(FIntPoint PlayerChunk);
};
