#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelBiome.h"
#include "../Trees/TreeGenerator.h"
#include "ChunkGenerationTask.h"
#include "../Features/VoxelFeatureGenerator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "ChunkWorldManager.generated.h"

class AWorldChunk;

/** Tracks HISM components belonging to a specific column. */
struct FColumnTreeInstances
{
	/** One HISM component per archetype, created locally for this column. */
	TArray<UHierarchicalInstancedStaticMeshComponent*> Components;
};

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
	int32 ChunkSize = 64;

	/** Vertical height of each chunk layer in voxel rows. Chunks stack vertically as needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	int32 ChunkHeight = 256;



	/** World-space size of one cube in UU. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	float VoxelSize = 100.0f;

	/** How many chunks around the player to keep loaded (Chebyshev radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Chunks")
	int32 RenderDistance = 64;

	// ── LOD distance control ────────────────────────────────────────────

	/**
	 * Single control for all LOD thresholds. Each LOD tier spans this many chunks:
	 *   LOD 0 (full):    0 .. LODBaseDistance
	 *   LOD 1 (half):    LODBaseDistance+1 .. 2×LODBaseDistance
	 *   LOD 2 (quarter): 2×LODBaseDistance+1 .. 3×LODBaseDistance
	 *   LOD 3 (eighth):  beyond 3×LODBaseDistance
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|LOD", meta = (ClampMin = "1"))
	int32 LODBaseDistance = 10;

	/** Number of cells per side for the LOD 3+ heightmap mesh.
	 *  1 = 1x1 cell = 2 triangles per distant chunk. Max 32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|LOD", meta = (ClampMin = "1", ClampMax = "32"))
	int32 HeightmapResolution = 1;

	/** How many LOD-3 chunk columns to merge into a single draw call (per side).
	 *  8 = 8x8 = 64 columns per actor → ~RenderDistance²/64 draw calls instead of RenderDistance². */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|LOD", meta = (ClampMin = "1", ClampMax = "32"))
	int32 HeightmapBatchSize = 8;

	// ── Noise parameters ────────────────────────────────────────────────

	/** World-space size of a Worley cell. Controls how large biome regions are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Biomes")
	float BiomeCellSize = 1000000.0f;

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

	/** The material to apply to water. If null, a translucent blue material is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Material")
	UMaterialInterface* WaterMaterial;

	/** The height below which empty blocks are filled with water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water")
	int32 WaterLevel = 10;

	/** Material applied to all trees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Material")
	UMaterialInterface* TreeMaterial;

	// ── Trees ───────────────────────────────────────────────────────────
	
	/** Properties for tree procedural generation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Trees")
	FVoxelTreeParams TreeParams;

	/** Number of unique base trees to generate to ensure variety */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Trees")
	int32 BaseTreeCount = 10;

	/** World-space size of a tree cell used for placement. Should be larger than biggest tree diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Trees")
	float TreeCellSize = 3500.0f;

	/** Maximum LOD level at which trees will still be generated and spawned. 0 = LOD 0 only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Trees", meta = (ClampMin = "0", ClampMax = "2"))
	int32 MaxTreeLOD = 2;

private:
	/** Currently loaded chunks, keyed by 3D chunk coordinate (X, Y, Z layer). */
	TMap<FIntVector, AWorldChunk*> LoadedChunks;

	/** Set of XY columns that have been loaded (all their vertical chunks). */
	TSet<FIntPoint> LoadedColumns;

	/** Set of XY columns that have an async generation task in flight, mapped to the LOD level of the task. */
	TMap<FIntPoint, int32> InFlightTasks;

	/** Queue of completed async generation results. */
	TQueue<FChunkGenerationResult, EQueueMode::Mpsc> FinishedTasksQueue;

	/** Last known player chunk coord – avoids redundant updates. */
	FIntPoint LastPlayerChunk;
	bool bHasLastPlayerChunk = false;

	/** Priority queue of XY column coordinates waiting to be loaded or updated (LOD transition).
	 *  Maintained as a binary max-heap: lowest LOD (highest detail) + closest distance at top. */
	TArray<FIntPoint> ColumnWorkQueue;

	/** Player chunk coord used when the heap was last built; kept stable between rebuilds. */
	FIntPoint HeapPlayerChunk;

	/** Currently active LOD level per loaded column. */
	TMap<FIntPoint, int32> ColumnLODs;

	/** Cached full-resolution max terrain height (voxel rows) per XY column.
	 *  Populated from ZLayer==0 results; used to bound Z-loop iteration on unload
	 *  and passed as a hint to re-generation tasks on LOD transitions. */
	TMap<FIntPoint, int32> ColumnMaxHeightCache;

	/** Cached material created at runtime if TerrainMaterial is null. */
	UPROPERTY()
	UMaterialInterface* CachedRuntimeMaterial;

	/** Cached material created at runtime if WaterMaterial is null. */
	UPROPERTY()
	UMaterialInterface* CachedRuntimeWaterMaterial;

	/** Array of generated base tree archetypes. Each archetype has its mesh pre-baked. */
	TArray<FVoxelTreeData> CachedBaseTrees;

	/** Thread-safe array of active feature generators passed to chunks */
	TArray<TSharedPtr<const IVoxelFeatureGenerator, ESPMode::ThreadSafe>> ActiveFeatures;

	/** Tree instances currently alive, keyed by chunk XY column coord. */
	TMap<FIntPoint, FColumnTreeInstances> ColumnTreeInstances;

	/** Baked static meshes for each tree archetype. */
	UPROPERTY()
	TArray<class UStaticMesh*> BakedTreeMeshes;

	/** Pre-generates tree archetypes and bakes their meshes once. */
	void GenerateTreeArchetypes();

	/** Adds instances to HISM for a column based on FeaturePlacements. */
	void UpdateTreeInstancesForColumn(FIntPoint Coord, const TArray<FFeaturePlacement>& Placements, int32 LODLevel);

	// ── LOD 3 Sector Batching ────────────────────────────────────────────
	// Instead of one actor per LOD-3 column (~14K draw calls), we group
	// HeightmapBatchSize×HeightmapBatchSize columns into one merged actor.

	struct FHeightmapSector
	{
		AWorldChunk* Actor = nullptr;
		/** Mesh data per column, stored in sector-local space. */
		TMap<FIntPoint, FVoxelMeshData> ColumnData;
		/** True when ColumnData changed and the actor needs re-uploading. */
		bool bDirty = false;
	};

	/** Live sector state, keyed by sector coordinate (column coord / HeightmapBatchSize). */
	TMap<FIntPoint, FHeightmapSector> HeightmapSectors;

	/** Maximum number of Z-layers to scan when unloading or processing chunks. */
	static constexpr int32 MaxZLayersLimit = 128;

	/** Ensures the terrain material is loaded or created. */
	void EnsureMaterial();

	/** Convert a world position to a horizontal chunk coordinate. */
	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;

	/** Unload (destroy) all vertical chunks for an XY column. */
	void UnloadChunkColumn(FIntPoint Coord);

	/** Full update: load nearby columns, unload distant ones. */
	void UpdateChunksAroundPlayer(FIntPoint PlayerChunk);

	/** Compute LOD tier for a column based on Chebyshev distance to the player chunk. */
	int32 CalculateLODForColumn(FIntPoint ColumnCoord, FIntPoint PlayerChunk) const;

	/** Dispatch async generation tasks for all Z layers of a column at the given LOD. */
	void DispatchChunkTasks(FIntPoint Coord, int32 LODLevel);

	/** Map a column coordinate to its sector coordinate. */
	FIntPoint GetSectorCoord(FIntPoint ColCoord) const;

	/** Add/update a column's heightmap data in its sector; marks sector dirty. */
	void AccumulateHeightmapColumn(FIntPoint ColCoord, FVoxelMeshData&& MeshData);

	/** Remove a column's heightmap data from its sector; marks sector dirty.
	 *  Destroys the sector actor if no columns remain. */
	void RemoveColumnFromSector(FIntPoint ColCoord);

	/** Re-upload all dirty sectors in one merged draw call each. */
	void FlushDirtySectors();

	/** Process the column work queue and dispatch async generation tasks. */
	void ProcessColumnWorkQueue();

	/** Process finished generation tasks and upload mesh data to GPU. */
	void ProcessFinishedTasks();

	/** Handle a finished heightmap result (LOD 3+). Returns actors to defer-destroy. */
	void HandleHeightmapResult(const FChunkGenerationResult& Result, TArray<AWorldChunk*>& OutPendingDestroy);

	/** Handle a finished voxel result (LOD 0-2). Returns true if mesh was uploaded. */
	bool HandleVoxelResult(const FChunkGenerationResult& Result);

	/** Clean up sector data and spawn tree actors after a voxel chunk mesh is applied. */
	void PostApplyChunk(const FChunkGenerationResult& Result, UMaterialInterface* Material);

	/** Returns true if column A should be processed before B (lower LOD = higher priority, then closer distance). */
	bool CompareColumnPriority(const FIntPoint& A, const FIntPoint& B) const;
};
