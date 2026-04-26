#include "WorldManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "VoxelObject.h"
#include "Trees/TreeGenerator.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"

UWorldManager::UWorldManager()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldManager::BeginPlay()
{
	Super::BeginPlay();
	InitializeEnvironment();
}

void UWorldManager::GenerateArchetypes(const FVoxelTreeParams& TreeParams, int32 BaseCount, float Seed, float VoxelSize, UMaterialInterface* Material)
{
	Shutdown();

	const FColor WoodColor(101, 67, 33, 255);
	const FColor LeavesColor(34, 139, 34, 255);

	for (int32 i = 0; i < BaseCount; ++i)
	{
		FVoxelTreeData TreeData = FTreeGenerator::GenerateTree(TreeParams, FMath::FloorToInt32(Seed) + i * 100);
		
		UVoxelObject::GenerateMeshData(TreeData.Grid, VoxelSize,
			[&](uint8 BlockType, const FVector& Pos, const FVector& Normal) -> FColor
			{
				return (BlockType == TREE_BLOCKTYPE_LEAVES) ? LeavesColor : WoodColor;
			},
			TreeData.BlockMeshes);
		
		FString MeshName = FString::Printf(TEXT("BakedTree_%d"), i);
		UStaticMesh* BakedMesh = UVoxelObject::BakeToStaticMesh(TreeData.BlockMeshes, GetOwner(), FName(*MeshName));
		if (BakedMesh)
		{
			FTreeGenerator::AddTrunkCollision(BakedMesh, TreeData.TrunkCollision, VoxelSize);
			for (int32 s = 0; s < TreeData.BlockMeshes.Num(); ++s)
			{
				BakedMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
			}
			
			BakedMeshes.Add(BakedMesh);
			CachedArchetypes.Add(TreeData);

			// Create the global HISM pool for this archetype
			UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
			HISM->SetStaticMesh(BakedMesh);
			HISM->SetCastShadow(true);
			HISM->bCastFarShadow = true;
			HISM->SetBoundsScale(5.0f); // Generous bounds to prevent premature culling
			
			HISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			HISM->SetCollisionObjectType(ECC_WorldStatic);
			HISM->SetCollisionResponseToAllChannels(ECR_Block);

			HISM->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			HISM->RegisterComponent();

			HISMContainers.Add(i, HISM);
			FreeIndices.FindOrAdd(i);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WorldManager: Generated %d archetypes with stable-index pooling."), BakedMeshes.Num());
}

void UWorldManager::UpdateInstancesForChunk(FIntPoint ChunkCoord, const TArray<FFeaturePlacement>& Placements, int32 LODLevel, float VoxelSize)
{
	// Optimization: If we already have instances for this chunk, skip the update.
	// Since tree placements are deterministic, they don't change between LOD 0/1/2.
	// This prevents the "Clear-then-Add" flicker during LOD transitions.
	if (ChunkToIndices.Contains(ChunkCoord))
	{
		return;
	}

	if (Placements.IsEmpty()) return;

	TSet<int32> AffectedPools;

	for (const FFeaturePlacement& Placement : Placements)
	{
		if (!CachedArchetypes.IsValidIndex(Placement.ArchetypeIndex)) continue;

		const FVoxelTreeData& Archetype = CachedArchetypes[Placement.ArchetypeIndex];
		FVector Pos = Placement.WorldPosition;
		Pos.X -= (Archetype.CenterOffset.X + 0.5f) * VoxelSize;
		Pos.Y -= (Archetype.CenterOffset.Y + 0.5f) * VoxelSize;
		Pos.Z -= Archetype.CenterOffset.Z * VoxelSize;

		FTransform Transform(FRotator::ZeroRotator, Pos);
		InternalAddInstance(ChunkCoord, Placement.ArchetypeIndex, Transform);
		AffectedPools.Add(Placement.ArchetypeIndex);
	}

	// Ensure the new instances are visible immediately
	for (int32 PoolIdx : AffectedPools)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = HISMContainers.FindRef(PoolIdx))
		{
			HISM->MarkRenderStateDirty();
		}
	}
}

void UWorldManager::InternalAddInstance(FIntPoint ChunkCoord, int32 ArchetypeIndex, const FTransform& Transform)
{
	UHierarchicalInstancedStaticMeshComponent* HISM = HISMContainers.FindRef(ArchetypeIndex);
	if (!HISM) return;

	int32 GlobalIndex = INDEX_NONE;

	// 1. Try to reuse a free index
	TArray<int32>& FreeList = FreeIndices.FindOrAdd(ArchetypeIndex);
	if (FreeList.Num() > 0)
	{
		GlobalIndex = FreeList.Pop();
		HISM->UpdateInstanceTransform(GlobalIndex, Transform, true, false, true);
	}
	else
	{
		// 2. Otherwise add a new one
		GlobalIndex = HISM->AddInstance(Transform, false);
	}

	if (GlobalIndex == INDEX_NONE) return;

	// Bookkeeping
	TMap<int32, TArray<int32>>& ChunkMap = ChunkToIndices.FindOrAdd(ChunkCoord);
	TArray<int32>& ChunkIndices = ChunkMap.FindOrAdd(ArchetypeIndex);
	ChunkIndices.Add(GlobalIndex);
}

void UWorldManager::ClearInstancesForChunk(FIntPoint ChunkCoord)
{
	TMap<int32, TArray<int32>>* ChunkMapPtr = ChunkToIndices.Find(ChunkCoord);
	if (!ChunkMapPtr) return;

	for (auto& Pair : *ChunkMapPtr)
	{
		int32 ArchetypeIndex = Pair.Key;
		TArray<int32>& Indices = Pair.Value;

		for (int32 GlobalIndex : Indices)
		{
			InternalRemoveInstance(ArchetypeIndex, GlobalIndex);
		}
	}

	ChunkToIndices.Remove(ChunkCoord);
}

void UWorldManager::InternalRemoveInstance(int32 ArchetypeIndex, int32 GlobalIndex)
{
	UHierarchicalInstancedStaticMeshComponent* HISM = HISMContainers.FindRef(ArchetypeIndex);
	if (!HISM) return;

	// 'Hide' the instance by moving it far away and scaling to zero.
	// This keeps the GlobalIndex stable so we don't have to swap and flicker.
	FTransform HiddenTransform(FRotator::ZeroRotator, FVector(0, 0, -1000000.0f), FVector::ZeroVector);
	HISM->UpdateInstanceTransform(GlobalIndex, HiddenTransform, true, false, true);

	// Add to free list for future reuse
	FreeIndices.FindOrAdd(ArchetypeIndex).Add(GlobalIndex);
}

void UWorldManager::Shutdown()
{
	ChunkToIndices.Empty();
	FreeIndices.Empty();
	
	for (auto& Pair : HISMContainers)
	{
		if (Pair.Value) Pair.Value->DestroyComponent();
	}
	HISMContainers.Empty();
	
	BakedMeshes.Empty();
	CachedArchetypes.Empty();
}

void UWorldManager::InitializeEnvironment()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// 1. Exponential Height Fog
	AExponentialHeightFog* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	if (!Fog)
	{
		Fog = World->SpawnActor<AExponentialHeightFog>();
	}
	if (Fog && Fog->GetComponent())
	{
		Fog->GetComponent()->FogDensity = 0.02f;
		Fog->GetComponent()->FogHeightFalloff = 0.2f;
		Fog->GetComponent()->bEnableVolumetricFog = true;
		Fog->GetComponent()->VolumetricFogScatteringDistribution = 0.8f;
		Fog->GetComponent()->VolumetricFogExtinctionScale = 2.0f;
	}

	// 2. Sky Atmosphere
	AActor* SkyActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->FindComponentByClass<USkyAtmosphereComponent>()) { SkyActor = *It; break; }
	}
	if (!SkyActor)
	{
		SkyActor = World->SpawnActor<AActor>(AActor::StaticClass());
		if (SkyActor)
		{
			USkyAtmosphereComponent* SkyComp = NewObject<USkyAtmosphereComponent>(SkyActor);
			SkyComp->RegisterComponent();
			SkyActor->SetRootComponent(SkyComp);
		}
	}

	// 3. Volumetric Clouds
	AActor* CloudActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->FindComponentByClass<UVolumetricCloudComponent>()) { CloudActor = *It; break; }
	}
	if (!CloudActor)
	{
		CloudActor = World->SpawnActor<AActor>(AActor::StaticClass());
		if (CloudActor)
		{
			UVolumetricCloudComponent* CloudComp = NewObject<UVolumetricCloudComponent>(CloudActor);
			CloudComp->RegisterComponent();
			CloudActor->SetRootComponent(CloudComp);
		}
	}

	// 4. Directional Light (ensure it casts cloud shadows)
	ADirectionalLight* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	if (Sun)
	{
		if (UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunComp->bCastCloudShadows = true;
			SunComp->bCastModulatedShadows = true;
			SunComp->Intensity = 10.0f;

			// Enable Far Shadows for the HISMs
			SunComp->FarShadowCascadeCount = 4;
			SunComp->FarShadowDistance = 1000000.0f; // 10km
		}
	}
}
