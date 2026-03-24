#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

	/** Base noise frequency (lower = bigger, smoother features). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	float Frequency = 0.001f;

	/** Maximum terrain height in voxel columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	float Amplitude = 3.0f;

	/** Number of noise octaves (more = more detail). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	int32 Octaves = 3;

	/** How quickly each octave's amplitude falls off (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	float Persistence = 0.35f;

	/** How quickly each octave's frequency increases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Noise")
	float Lacunarity = 2.2f;

	/** World seed – different seeds produce different landscapes. */
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
