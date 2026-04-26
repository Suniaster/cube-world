#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Features/VoxelFeatureGenerator.h"
#include "Trees/TreeGenerator.h"
#include "WorldManager.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/** Tracks the owner of a specific instance in a global HISM pool. */
struct FInstanceOwner
{
	FIntPoint ChunkCoord;
	int32 InternalIdx; // Index within the Chunk's own instance list for this archetype
};

/** 
 * Decoupled manager for all non-voxel "objects" in the world (trees, vegetation, structures).
 * Handles global HISM pooling and robust swap-and-pop instance bookkeeping.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUBEWORLD_API UWorldManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldManager();

	/** Generates and bakes archetypes into static meshes. */
	void GenerateArchetypes(const FVoxelTreeParams& TreeParams, int32 BaseCount, float Seed, float VoxelSize, UMaterialInterface* Material);

	/** Updates (replaces) all instances for a specific chunk. */
	void UpdateInstancesForChunk(FIntPoint ChunkCoord, const TArray<FFeaturePlacement>& Placements, int32 LODLevel, float VoxelSize);

	/** Removes all instances associated with a chunk. */
	void ClearInstancesForChunk(FIntPoint ChunkCoord);

	/** Clears all pools and registries. */
	void Shutdown();

protected:
	/** Global HISM pools mapped by ArchetypeIndex. */
	UPROPERTY()
	TMap<int32, UHierarchicalInstancedStaticMeshComponent*> HISMContainers;

	/** Maps ChunkCoord -> (ArchetypeIndex -> List of Global HISMPool Indices) */
	TMap<FIntPoint, TMap<int32, TArray<int32>>> ChunkToIndices;

	/** Pooled indices that are currently 'hidden' (scaled to zero) and ready for reuse. */
	TMap<int32, TArray<int32>> FreeIndices;

	/** Pre-generated tree archetypes used for mesh and bounds info. */
	TArray<FVoxelTreeData> CachedArchetypes;

	/** Baked meshes for HISMs. */
	UPROPERTY()
	TArray<UStaticMesh*> BakedMeshes;

private:
	/** Internal helper to remove a single global instance and handle swap-and-pop. */
	void InternalRemoveInstance(int32 ArchetypeIndex, int32 GlobalIndex);

	/** Internal helper to add an instance and update bookkeeping. */
	void InternalAddInstance(FIntPoint ChunkCoord, int32 ArchetypeIndex, const FTransform& Transform);
};
