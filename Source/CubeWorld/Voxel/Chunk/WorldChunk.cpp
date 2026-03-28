#include "WorldChunk.h"
#include "VoxelTerrainNoise.h"
#include "VoxelBiome.h"
#include "../VoxelObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
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


void AWorldChunk::ApplyGeneratedMesh(const FIntVector& InKey, const FVoxelMeshData& InMeshData, UMaterialInterface* InMaterial, int32 InLODLevel)
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
	VoxelObject->GetMeshData() = InMeshData;

	// Create/Update Dynamic Material
	UMaterialInstanceDynamic* DynMaterial = nullptr;
	if (InMaterial)
	{
		if (VoxelObject->GetMeshComponent())
		{
			DynMaterial = Cast<UMaterialInstanceDynamic>(VoxelObject->GetMeshComponent()->GetMaterial(0));
		}

		if (!DynMaterial || DynMaterial->Parent != InMaterial)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(InMaterial, this);
		}
	}

	bool bCreateCollision = (LODLevel == 0);
	VoxelObject->Spawn(this, DynMaterial ? (UMaterialInterface*)DynMaterial : InMaterial, bCreateCollision);

	// LOD 0 needs collision for gameplay; distant LOD chunks skip it to save physics overhead.
	if (VoxelObject->GetMeshComponent())
	{
		if (LODLevel == 0)
		{
			VoxelObject->GetMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			VoxelObject->GetMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}
