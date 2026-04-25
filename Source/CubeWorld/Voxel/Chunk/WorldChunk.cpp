#include "WorldChunk.h"
#include "../VoxelObject.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"
#include "../VoxelTypes.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#endif

AWorldChunk::AWorldChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SetRootComponent(Root);
}

// ── Chunk generation ────────────────────────────────────────────────────────


void AWorldChunk::ApplyGeneratedMesh(const FIntVector& InKey, const FVoxelMeshData& InHeightmapData, const TMap<uint8, FVoxelMeshData>& InBlockMeshes, UMaterialInterface* InMaterial, UMaterialInterface* InWaterMaterial, int32 InLODLevel)
{
	ChunkCoord = FIntPoint(InKey.X, InKey.Y);
	ZLayer = InKey.Z;
	LODLevel = InLODLevel;

	if (!VoxelObject)
	{
		VoxelObject = NewObject<UVoxelObject>(this);
	}

	// Copy the mesh data into the VoxelObject
	// Note: UVoxelObject::Spawn will clear it after GPU upload to save memory.
	VoxelObject->GetHeightmapData() = InHeightmapData;
	VoxelObject->GetBlockMeshes() = InBlockMeshes;

	const bool bCreateCollision = (LODLevel == 0);
	VoxelObject->Spawn(this, InMaterial, InWaterMaterial, bCreateCollision);

	// LOD 0 needs collision for gameplay; distant LOD chunks skip it to save physics overhead.
	if (UProceduralMeshComponent* MC = VoxelObject->GetMeshComponent())
	{
		if (LODLevel == 0)
		{
			MC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			MC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		// LOD 3+ chunks are heightmap-based distant terrain.
		// Disable all expensive rendering features that are imperceptible at that range.
		const bool bIsDistant = (LODLevel >= 3);
		MC->SetCastShadow(!bIsDistant);
		MC->bCastDynamicShadow                  = !bIsDistant;
		MC->bAffectDynamicIndirectLighting      = !bIsDistant;
		MC->bAffectDistanceFieldLighting        = !bIsDistant;
		MC->bVisibleInRayTracing                = !bIsDistant;
	}
}
