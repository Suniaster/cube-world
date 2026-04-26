#include "ChunkWorldManager.h"
#include "WorldChunk.h"
#include "VoxelBiome.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "ChunkGenerationTask.h"
#include "../Features/VoxelTreePlacerFeature.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#endif

AChunkWorldManager::AChunkWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
	Biomes = GetDefaultBiomeParams();
}

void AChunkWorldManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureMaterial();
	GenerateTreeArchetypes();

	// Register active feature generators (placement-only, no voxel stamping)
	ActiveFeatures.Add(MakeShared<FVoxelTreePlacerFeature, ESPMode::ThreadSafe>(TreeCellSize, 1500.0f, BaseTreeCount));

	// Force an initial chunk load around origin
	FIntPoint Origin(0, 0);
	UpdateChunksAroundPlayer(Origin);
}

void AChunkWorldManager::GenerateTreeArchetypes()
{
	UMaterialInterface* UseMat = TreeMaterial ? TreeMaterial : (TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial);
	if (!UseMat) return;

	CachedBaseTrees.Empty();
	BakedTreeMeshes.Empty();

	const FColor WoodColor(101, 67, 33, 255);
	const FColor LeavesColor(34, 139, 34, 255);

	for (int32 i = 0; i < BaseTreeCount; ++i)
	{
		FVoxelTreeData TreeData = FTreeGenerator::GenerateTree(TreeParams, FMath::FloorToInt32(Seed) + i * 100);
		
		// Generate voxel mesh data for the tree archetype
		UVoxelObject::GenerateMeshData(TreeData.Grid, VoxelSize,
			[&](uint8 BlockType, const FVector& Pos, const FVector& Normal) -> FColor
			{
				return (BlockType == TREE_BLOCKTYPE_LEAVES) ? LeavesColor : WoodColor;
			},
			TreeData.BlockMeshes);
		
		// Bake the mesh data into a StaticMesh for high-performance instancing
		FString MeshName = FString::Printf(TEXT("BakedTree_%d"), i);
		UStaticMesh* BakedMesh = UVoxelObject::BakeToStaticMesh(TreeData.BlockMeshes, this, FName(*MeshName));
		if (BakedMesh)
		{
			FTreeGenerator::AddTrunkCollision(BakedMesh, TreeData.TrunkCollision, VoxelSize);

			// Apply material to all sections (Wood and Leaves)
			for (int32 s = 0; s < TreeData.BlockMeshes.Num(); ++s)
			{
				BakedMesh->GetStaticMaterials().Add(FStaticMaterial(UseMat));
			}
			
			BakedTreeMeshes.Add(BakedMesh);
			CachedBaseTrees.Add(TreeData);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("Voxel: Generated %d tree archetypes and baked meshes."), BakedTreeMeshes.Num());
}

void AChunkWorldManager::EnsureMaterial()
{
	// Load user-created materials from expected paths if not already assigned
	if (!TerrainMaterial && !CachedRuntimeMaterial)
	{
		CachedRuntimeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_VoxelTerrain.M_VoxelTerrain"));
	}

	if (!WaterMaterial && !CachedRuntimeWaterMaterial)
	{
		CachedRuntimeWaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_VoxelWater.M_VoxelWater"));
	}

#if WITH_EDITOR
	if (!CachedRuntimeMaterial)
	{
		// Create a DefaultLit material that uses vertex color for biome coloring
		UMaterial* NewMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_VoxelTerrain_Runtime"));

		// Vertex Color node → Base Color
		UMaterialExpressionVertexColor* VertColorExpr = NewObject<UMaterialExpressionVertexColor>(NewMat);
		NewMat->GetExpressionCollection().AddExpression(VertColorExpr);

		if (UMaterialEditorOnlyData* EditorData = NewMat->GetEditorOnlyData())
		{
			EditorData->BaseColor.Expression = VertColorExpr;
		}

		NewMat->PreEditChange(nullptr);
		NewMat->PostEditChange();

		CachedRuntimeMaterial = NewMat;
	}
#endif
}

void AChunkWorldManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Find the player's pawn
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	APawn* PlayerPawn = PC->GetPawn();
	if (!PlayerPawn) return;

	FVector PlayerLoc = PlayerPawn->GetActorLocation();
	FIntPoint CurrentChunk = WorldToChunkCoord(PlayerLoc);

	// Only update the generation goal when the player crosses a chunk boundary
	if (!bHasLastPlayerChunk || CurrentChunk != LastPlayerChunk)
	{
		LastPlayerChunk = CurrentChunk;
		bHasLastPlayerChunk = true;
		UpdateChunksAroundPlayer(CurrentChunk);
	}

	ProcessColumnWorkQueue();
	ProcessFinishedTasks();
}

FIntPoint AChunkWorldManager::WorldToChunkCoord(const FVector& WorldPos) const
{
	float ChunkWorldSize = static_cast<float>(ChunkSize) * VoxelSize;
	int32 CX = FMath::FloorToInt32(WorldPos.X / ChunkWorldSize);
	int32 CY = FMath::FloorToInt32(WorldPos.Y / ChunkWorldSize);
	return FIntPoint(CX, CY);
}

void AChunkWorldManager::UpdateChunksAroundPlayer(FIntPoint PlayerChunk)
{
	HeapPlayerChunk = PlayerChunk;

	// 1. Determine which XY columns should be loaded
	TSet<FIntPoint> DesiredColumns;
	for (int32 DX = -RenderDistance; DX <= RenderDistance; ++DX)
	{
		for (int32 DY = -RenderDistance; DY <= RenderDistance; ++DY)
		{
			DesiredColumns.Add(FIntPoint(PlayerChunk.X + DX, PlayerChunk.Y + DY));
		}
	}

	// 2. Unload columns that are no longer needed
	TArray<FIntPoint> ToUnload;
	for (FIntPoint Col : LoadedColumns)
	{
		if (!DesiredColumns.Contains(Col))
		{
			ToUnload.Add(Col);
		}
	}
	for (FIntPoint Coord : ToUnload)
	{
		UnloadChunkColumn(Coord);
	}

	// Remove no-longer-desired columns from the work queue
	for (int32 i = ColumnWorkQueue.Num() - 1; i >= 0; --i)
	{
		if (!DesiredColumns.Contains(ColumnWorkQueue[i]))
		{
			ColumnWorkQueue.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}

	// Build a fast lookup set for O(1) checks during the DesiredColumns iteration
	TSet<FIntPoint> WorkQueueSet(ColumnWorkQueue);

	// 3. For each desired column, compute LOD and queue if needed
	for (FIntPoint Coord : DesiredColumns)
	{
		int32 TargetLOD = CalculateLODForColumn(Coord, PlayerChunk);

		bool bNew = !LoadedColumns.Contains(Coord);
		bool bLODChanged = LoadedColumns.Contains(Coord) && ColumnLODs.FindRef(Coord) != TargetLOD;

		if ((bNew || bLODChanged) && !WorkQueueSet.Contains(Coord))
		{
			// NOTE: We intentionally do NOT call RemoveColumnFromSector here on LOD 3 → lower
			// transitions. The sector column data acts as a visual placeholder until the
			// replacement voxel mesh arrives. Removal happens in the result processing path.
			ColumnLODs.Add(Coord, TargetLOD);
			ColumnWorkQueue.Add(Coord);
			WorkQueueSet.Add(Coord);
		}
	}

	// Rebuild max-heap: lowest LOD (highest detail) + closest distance at the top.
	// O(n) heapify replaces the previous O(n log n) Sort; between rebuilds each
	// HeapPopDiscard in Tick costs only O(log n) instead of O(n).
	ColumnWorkQueue.Heapify([this](const FIntPoint& A, const FIntPoint& B) { return CompareColumnPriority(A, B); });
}

int32 AChunkWorldManager::CalculateLODForColumn(FIntPoint ColumnCoord, FIntPoint PlayerChunk) const
{
	// Chebyshev distance
	int32 Dist = FMath::Max(FMath::Abs(ColumnCoord.X - PlayerChunk.X), FMath::Abs(ColumnCoord.Y - PlayerChunk.Y));

	if (Dist <= LODBaseDistance)     return 0;
	if (Dist <= LODBaseDistance * 2) return 1;
	if (Dist <= LODBaseDistance * 3) return 2;
	return 3;
}

void AChunkWorldManager::UnloadChunkColumn(FIntPoint Coord)
{
	// Remove HISM instances for this column
	if (FColumnTreeInstances* Instances = ColumnTreeInstances.Find(Coord))
	{
		for (auto& Pair : Instances->Components)
		{
			if (Pair.Value) Pair.Value->DestroyComponent();
		}
		ColumnTreeInstances.Remove(Coord);
	}

	// Use the cached max height to limit the Z scan; fall back to MaxZLayersLimit if unknown.
	const int32 CachedMaxHeight = ColumnMaxHeightCache.FindRef(Coord);
	const int32 MaxZLayers = (CachedMaxHeight > 0)
		? FMath::Clamp(FMath::CeilToInt32(static_cast<float>(CachedMaxHeight) / static_cast<float>(ChunkHeight)) + 1, 1, MaxZLayersLimit)
		: MaxZLayersLimit;

	for (int32 Z = 0; Z < MaxZLayers; ++Z)
	{
		FIntVector Key(Coord.X, Coord.Y, Z);
		AWorldChunk** Found = LoadedChunks.Find(Key);
		if (Found && *Found)
		{
			(*Found)->Destroy();
			LoadedChunks.Remove(Key);
		}
	}
	InFlightTasks.Remove(Coord);
	ColumnMaxHeightCache.Remove(Coord);


	// If this column was part of a LOD 3 sector, evict it so the sector is rebuilt.
	RemoveColumnFromSector(Coord);

	LoadedColumns.Remove(Coord);
	ColumnLODs.Remove(Coord);
}

void AChunkWorldManager::DispatchChunkTasks(FIntPoint Coord, int32 LODLevel)
{
	EnsureMaterial();

	// Skip if a column async task at the same LOD is already in flight
	int32* ExistingLOD = InFlightTasks.Find(Coord);
	if (ExistingLOD && *ExistingLOD == LODLevel) return;

	InFlightTasks.Add(Coord, LODLevel);

	(new FAutoDeleteAsyncTask<FChunkGenerationTask>(
		Coord,
		ChunkSize,
		ChunkHeight,
		VoxelSize,
		LODLevel,
		BiomeCellSize,
		Seed,
		Biomes,
		BiomeBlendWidth,
		&FinishedTasksQueue,
		WaterLevel,
		ColumnMaxHeightCache.FindRef(Coord),
		HeightmapResolution,
		MaxTreeLOD,
		ActiveFeatures
	))->StartBackgroundTask();
}

// ── LOD 3 Sector Helpers ────────────────────────────────────────────────────

FIntPoint AChunkWorldManager::GetSectorCoord(FIntPoint ColCoord) const
{
	return FIntPoint(
		FMath::FloorToInt32(static_cast<float>(ColCoord.X) / static_cast<float>(HeightmapBatchSize)),
		FMath::FloorToInt32(static_cast<float>(ColCoord.Y) / static_cast<float>(HeightmapBatchSize)));
}

void AChunkWorldManager::AccumulateHeightmapColumn(FIntPoint ColCoord, FVoxelMeshData&& MeshData)
{
	const FIntPoint SectorCoord  = GetSectorCoord(ColCoord);
	const FIntPoint SectorOrigin = SectorCoord * HeightmapBatchSize;

	// Offset all vertices into sector-local space so the sector actor can sit at the sector origin.
	const float OffsetX = static_cast<float>(ColCoord.X - SectorOrigin.X) * ChunkSize * VoxelSize;
	const float OffsetY = static_cast<float>(ColCoord.Y - SectorOrigin.Y) * ChunkSize * VoxelSize;
	if (OffsetX != 0.0f || OffsetY != 0.0f)
	{
		for (FVector& V : MeshData.Vertices)
		{
			V.X += OffsetX;
			V.Y += OffsetY;
		}
	}

	FHeightmapSector& Sector       = HeightmapSectors.FindOrAdd(SectorCoord);
	Sector.ColumnData.Add(ColCoord, MoveTemp(MeshData));
	Sector.bDirty = true;
}

void AChunkWorldManager::RemoveColumnFromSector(FIntPoint ColCoord)
{
	const FIntPoint SectorCoord = GetSectorCoord(ColCoord);
	FHeightmapSector* Sector    = HeightmapSectors.Find(SectorCoord);
	if (!Sector) return;

	if (Sector->ColumnData.Remove(ColCoord) > 0)
	{
		if (Sector->ColumnData.IsEmpty())
		{
			// No columns left — destroy the actor and remove the sector entry.
			if (Sector->Actor)
			{
				Sector->Actor->Destroy();
			}
			HeightmapSectors.Remove(SectorCoord);
		}
		else
		{
			Sector->bDirty = true;
		}
	}
}

void AChunkWorldManager::FlushDirtySectors()
{
	EnsureMaterial();
	UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;

	for (auto& [SectorCoord, Sector] : HeightmapSectors)
	{
		if (!Sector.bDirty) continue;
		Sector.bDirty = false;

		FVoxelMeshData Merged;
		for (auto& [ColCoord, ColMesh] : Sector.ColumnData)
		{
			const int32 BaseIdx = Merged.Vertices.Num();
			Merged.Vertices.Append(ColMesh.Vertices);
			Merged.Normals.Append(ColMesh.Normals);
			Merged.Colors.Append(ColMesh.Colors);
			for (int32 Tri : ColMesh.Triangles)
			{
				Merged.Triangles.Add(BaseIdx + Tri);
			}
		}

		const FIntPoint SectorOrigin = SectorCoord * HeightmapBatchSize;
		const FVector SectorWorldPos(
			static_cast<float>(SectorOrigin.X) * ChunkSize * VoxelSize,
			static_cast<float>(SectorOrigin.Y) * ChunkSize * VoxelSize,
			0.0f);

		if (!Sector.Actor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			Sector.Actor = GetWorld()->SpawnActor<AWorldChunk>(
				AWorldChunk::StaticClass(), SectorWorldPos, FRotator::ZeroRotator, SpawnParams);
		}

		if (Sector.Actor)
		{
			// LOD 3 heightmap sectors never contain water — pass nullptr for the water material.
			const FIntVector SectorKey(SectorCoord.X, SectorCoord.Y, 0);
			Sector.Actor->ApplyGeneratedMesh(SectorKey, Merged, TMap<uint8, FVoxelMeshData>{}, UseMat, nullptr, 3);
		}
	}
}

bool AChunkWorldManager::CompareColumnPriority(const FIntPoint& A, const FIntPoint& B) const
{
	const int32 LODA = ColumnLODs.FindRef(A);
	const int32 LODB = ColumnLODs.FindRef(B);
	if (LODA != LODB) return LODA < LODB;
	const float DistA = FVector2D::DistSquared(FVector2D(A), FVector2D(HeapPlayerChunk));
	const float DistB = FVector2D::DistSquared(FVector2D(B), FVector2D(HeapPlayerChunk));
	return DistA < DistB;
}

void AChunkWorldManager::PostApplyChunk(const FChunkGenerationResult& Result, UMaterialInterface* Material)
{
	RemoveColumnFromSector(Result.ChunkCoord);
	if (Result.ZLayer == 0)
	{
		UpdateTreeInstancesForColumn(Result.ChunkCoord, Result.FeaturePlacements, Result.LODLevel);
	}
}

void AChunkWorldManager::ProcessColumnWorkQueue()
{
	auto WorkQueueComp = [this](const FIntPoint& A, const FIntPoint& B) { return CompareColumnPriority(A, B); };

	int32 JobsProcessed = 0;
	const int32 MaxConcurrentTasks = 64;
	const int32 MaxJobsPerFrame = FMath::Max(MaxChunksPerFrame * 4, 20);

	while (ColumnWorkQueue.Num() > 0 && InFlightTasks.Num() < MaxConcurrentTasks && JobsProcessed < MaxJobsPerFrame)
	{
		FIntPoint Coord = ColumnWorkQueue.HeapTop();
		ColumnWorkQueue.HeapPopDiscard(WorkQueueComp);

		int32 TargetLOD = ColumnLODs.FindRef(Coord);
		if (!LoadedColumns.Contains(Coord))
		{
			LoadedColumns.Add(Coord);
		}
		DispatchChunkTasks(Coord, TargetLOD);
		JobsProcessed++;
	}
}

void AChunkWorldManager::ProcessFinishedTasks()
{
	int32 UploadsThisFrame = 0;
	FChunkGenerationResult FinishedResult;
	TArray<AWorldChunk*> PendingDestroy;

	while (UploadsThisFrame < MaxChunksPerFrame && FinishedTasksQueue.Dequeue(FinishedResult))
	{
		// If a newer LOD task was dispatched, this result is stale — discard it.
		int32* InFlightLOD = InFlightTasks.Find(FinishedResult.ChunkCoord);
		if (InFlightLOD && *InFlightLOD != FinishedResult.LODLevel)
		{
			continue;
		}
		InFlightTasks.Remove(FinishedResult.ChunkCoord);

		// If column was unloaded or LOD changed since task started, discard
		if (!LoadedColumns.Contains(FinishedResult.ChunkCoord) ||
			ColumnLODs.FindRef(FinishedResult.ChunkCoord) != FinishedResult.LODLevel)
		{
			continue;
		}

		// Update the max-height cache from the ZLayer 0 result
		if (FinishedResult.ZLayer == 0 && FinishedResult.ColumnMaxHeight > 0)
		{
			int32& Cached = ColumnMaxHeightCache.FindOrAdd(FinishedResult.ChunkCoord);
			Cached = FMath::Max(Cached, FinishedResult.ColumnMaxHeight);
		}

		// LOD 3 heightmap results go into the sector batch system
		if (FinishedResult.bIsHeightmap)
		{
			HandleHeightmapResult(FinishedResult, PendingDestroy);
			continue;
		}

		// Standard voxel result
		if (HandleVoxelResult(FinishedResult))
		{
			UploadsThisFrame++;
		}
	}

	// Upload sector meshes first, THEN destroy the old voxel actors they replaced
	FlushDirtySectors();
	for (AWorldChunk* Actor : PendingDestroy)
	{
		if (Actor) Actor->Destroy();
	}
}

void AChunkWorldManager::ClearTreeInstancesForColumn(FIntPoint Coord)
{
	if (FColumnTreeInstances* Instances = ColumnTreeInstances.Find(Coord))
	{
		for (auto& Pair : Instances->Components)
		{
			if (Pair.Value)
			{
				Pair.Value->ClearInstances();
			}
		}
	}
}

void AChunkWorldManager::UpdateTreeInstancesForColumn(FIntPoint Coord, const TArray<FFeaturePlacement>& Placements, int32 LODLevel)
{
	// 1. If we already have HISMs for this column, just clear their instances for reuse.
	// This is MUCH faster than destroying/recreating components.
	FColumnTreeInstances* TargetInstances = ColumnTreeInstances.Find(Coord);
	if (TargetInstances)
	{
		for (auto& Pair : TargetInstances->Components)
		{
			if (Pair.Value)
			{
				Pair.Value->ClearInstances();
				Pair.Value->MarkRenderStateDirty();
			}
		}
	}

	if (Placements.IsEmpty()) return;

	// 2. If no entry exists yet, create it.
	if (!TargetInstances)
	{
		TargetInstances = &ColumnTreeInstances.Add(Coord);
	}

	int32 AddedCount = 0;
	const bool bHasCollision = (LODLevel == 0);

	for (const FFeaturePlacement& Placement : Placements)
	{
		if (!BakedTreeMeshes.IsValidIndex(Placement.ArchetypeIndex)) continue;

		UHierarchicalInstancedStaticMeshComponent* HISM = TargetInstances->Components.FindRef(Placement.ArchetypeIndex);

		if (!HISM)
		{
			HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			HISM->SetStaticMesh(BakedTreeMeshes[Placement.ArchetypeIndex]);

			// Only LOD 0 trees have collision
			HISM->bAlwaysCreatePhysicsState = bHasCollision;
			HISM->SetCollisionEnabled(bHasCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			if (bHasCollision)
			{
				HISM->SetCollisionObjectType(ECC_WorldStatic);
				HISM->SetCollisionResponseToAllChannels(ECR_Block);
			}

			HISM->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			HISM->RegisterComponent();

			TargetInstances->Components.Add(Placement.ArchetypeIndex, HISM);
		}
		else
		{
			// Update collision settings if LOD changed but we are reusing the component
			if (HISM->GetCollisionEnabled() != (bHasCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision))
			{
				HISM->SetCollisionEnabled(bHasCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
				HISM->bAlwaysCreatePhysicsState = bHasCollision;
			}
		}

		const FVoxelTreeData& Archetype = CachedBaseTrees[Placement.ArchetypeIndex];
		FVector Pos = Placement.WorldPosition;
		Pos.X -= (Archetype.CenterOffset.X + 0.5f) * VoxelSize;
		Pos.Y -= (Archetype.CenterOffset.Y + 0.5f) * VoxelSize;
		Pos.Z -= Archetype.CenterOffset.Z * VoxelSize;

		FTransform Transform(FRotator::ZeroRotator, Pos);
		HISM->AddInstance(Transform, false);
		AddedCount++;
	}

	// 3. Mark dirty to trigger a re-render. 
	// Removed synchronous BuildTreeIfOutdated(true, true) and RecreatePhysicsState()
	// as they cause massive lag spikes. UE will handle the updates lazily/optimally.
	for (auto& Pair : TargetInstances->Components)
	{
		if (Pair.Value)
		{
			Pair.Value->MarkRenderStateDirty();
		}
	}

	if (AddedCount > 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Voxel: Updated %d tree instances in column (%d, %d) at LOD %d"), AddedCount, Coord.X, Coord.Y, LODLevel);
	}
}

void AChunkWorldManager::HandleHeightmapResult(const FChunkGenerationResult& Result, TArray<AWorldChunk*>& OutPendingDestroy)
{
	// Ensure tree instances are cleared for heightmap LODs (LOD 3+)
	ClearTreeInstancesForColumn(Result.ChunkCoord);

	// Defer eviction of old Z-layer voxel actors until after FlushDirtySectors()
	// uploads the replacement sector mesh. This prevents a 1-frame visibility gap.
	for (int32 Z = 0; Z < MaxZLayersLimit; ++Z)
	{
		FIntVector LayerKey(Result.ChunkCoord.X, Result.ChunkCoord.Y, Z);
		AWorldChunk** Found = LoadedChunks.Find(LayerKey);
		if (Found && *Found)
		{
			OutPendingDestroy.Add(*Found);
			LoadedChunks.Remove(LayerKey);
		}
	}

	if (Result.bSuccess && Result.bHasAnyBlocks)
	{
		AccumulateHeightmapColumn(Result.ChunkCoord, const_cast<FVoxelMeshData&&>(Result.HeightmapData));
	}
	else
	{
		RemoveColumnFromSector(Result.ChunkCoord);
	}
}

bool AChunkWorldManager::HandleVoxelResult(const FChunkGenerationResult& Result)
{
	EnsureMaterial();
	UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;
	UMaterialInterface* UseWaterMat = WaterMaterial ? WaterMaterial : CachedRuntimeWaterMaterial;

	const FIntVector Key(Result.ChunkCoord.X, Result.ChunkCoord.Y, Result.ZLayer);

	if (Result.bSuccess && Result.bHasAnyBlocks)
	{
		// Check if chunk already exists (LOD transition: update mesh in-place)
		AWorldChunk** ExistingChunkPtr = LoadedChunks.Find(Key);
		if (ExistingChunkPtr && *ExistingChunkPtr)
		{
			(*ExistingChunkPtr)->ApplyGeneratedMesh(Key, Result.HeightmapData, Result.BlockMeshes, UseMat, UseWaterMat, Result.LODLevel);
			PostApplyChunk(Result, UseMat);
			return true;
		}

		// Spawn new chunk actor
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;

		const FVector ChunkLocation(
			static_cast<float>(Result.ChunkCoord.X) * ChunkSize * VoxelSize,
			static_cast<float>(Result.ChunkCoord.Y) * ChunkSize * VoxelSize,
			static_cast<float>(Result.ZLayer) * ChunkHeight * VoxelSize);

		AWorldChunk* NewChunk = GetWorld()->SpawnActor<AWorldChunk>(
			AWorldChunk::StaticClass(), ChunkLocation, FRotator::ZeroRotator, SpawnParams);

		if (NewChunk)
		{
			NewChunk->ApplyGeneratedMesh(Key, Result.HeightmapData, Result.BlockMeshes, UseMat, UseWaterMat, Result.LODLevel);
			LoadedChunks.Add(Key, NewChunk);
			PostApplyChunk(Result, UseMat);
			return true;
		}
	}
	else
	{
		// Empty at this LOD. If a chunk actor exists from a previous LOD, destroy it.
		AWorldChunk** ExistingChunkPtr = LoadedChunks.Find(Key);
		if (ExistingChunkPtr && *ExistingChunkPtr)
		{
			(*ExistingChunkPtr)->Destroy();
			LoadedChunks.Remove(Key);
		}

		// Even if empty, ZLayer 0 must call PostApplyChunk to update trees for this column
		if (Result.ZLayer == 0)
		{
			PostApplyChunk(Result, UseMat);
		}
	}
	return false;
}
