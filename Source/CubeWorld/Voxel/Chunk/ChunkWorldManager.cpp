#include "ChunkWorldManager.h"
#include "WorldChunk.h"
#include "VoxelBiome.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "ChunkGenerationTask.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
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

	// Force an initial chunk load around origin
	FIntPoint Origin(0, 0);
	UpdateChunksAroundPlayer(Origin);
}

void AChunkWorldManager::EnsureMaterial()
{
	if (TerrainMaterial) return;
	if (CachedRuntimeMaterial) return;

	// Try to load a user-created material first
	CachedRuntimeMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/M_VoxelTerrain.M_VoxelTerrain"));

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

	// ── Process the Column Work Queue (Thread Dispatch) ──
	int32 JobsProcessed = 0;
	int32 MaxConcurrentTasks = 64; // CPU core thread scale saturation point
	while (ColumnWorkQueue.Num() > 0 && InFlightTasks.Num() < MaxConcurrentTasks && JobsProcessed < FMath::Max(MaxChunksPerFrame * 4, 20))
	{
		FIntPoint Coord = ColumnWorkQueue[0];
		ColumnWorkQueue.RemoveAt(0);

		int32 TargetLOD = ColumnLODs.FindRef(Coord);
		if (!LoadedColumns.Contains(Coord))
		{
			DispatchChunkTasks(Coord, TargetLOD);
			LoadedColumns.Add(Coord);
		}
		else
		{
			DispatchChunkTasks(Coord, TargetLOD);
		}
		JobsProcessed++;
	}

	// ── Process Finished Tasks (Budgeted GPU Uploads) ──
	int32 UploadsThisFrame = 0;
	FChunkGenerationResult FinishedResult;
	while (UploadsThisFrame < MaxChunksPerFrame && FinishedTasksQueue.Dequeue(FinishedResult))
	{
		FIntVector Key(FinishedResult.ChunkCoord.X, FinishedResult.ChunkCoord.Y, FinishedResult.ZLayer);

		// If a newer LOD task was dispatched, this result is stale — discard it.
		int32* InFlightLOD = InFlightTasks.Find(FinishedResult.ChunkCoord);
		if (InFlightLOD && *InFlightLOD != FinishedResult.LODLevel)
		{
			continue;
		}
		// Safely clear the tracking flag as the column is now delivering results
		InFlightTasks.Remove(FinishedResult.ChunkCoord);

		// If column was unloaded or LOD changed since task started, discard
		if (!LoadedColumns.Contains(FinishedResult.ChunkCoord) || 
			ColumnLODs.FindRef(FinishedResult.ChunkCoord) != FinishedResult.LODLevel)
		{
			continue;
		}

		EnsureMaterial();
		UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;

		if (FinishedResult.bSuccess && FinishedResult.bHasAnyBlocks)
		{
			// Check if chunk already exists (LOD transition: update mesh in-place)
			AWorldChunk** ExistingChunkPtr = LoadedChunks.Find(Key);
			if (ExistingChunkPtr && *ExistingChunkPtr)
			{
				(*ExistingChunkPtr)->ApplyGeneratedMesh(Key, FinishedResult.MeshData, UseMat, FinishedResult.LODLevel);
				UploadsThisFrame++;
			}
			else
			{
				// Spawn new chunk actor
				FActorSpawnParameters SpawnParams;
				SpawnParams.Owner = this;

				float ChunkWorldX = static_cast<float>(FinishedResult.ChunkCoord.X) * ChunkSize * VoxelSize;
				float ChunkWorldY = static_cast<float>(FinishedResult.ChunkCoord.Y) * ChunkSize * VoxelSize;
				float ChunkWorldZ = static_cast<float>(FinishedResult.ZLayer) * ChunkHeight * VoxelSize;
				FVector ChunkLocation(ChunkWorldX, ChunkWorldY, ChunkWorldZ);

				AWorldChunk* NewChunk = GetWorld()->SpawnActor<AWorldChunk>(
					AWorldChunk::StaticClass(),
					ChunkLocation,
					FRotator::ZeroRotator,
					SpawnParams);

				if (NewChunk)
				{
					NewChunk->ApplyGeneratedMesh(Key, FinishedResult.MeshData, UseMat, FinishedResult.LODLevel);
					LoadedChunks.Add(Key, NewChunk);
					UploadsThisFrame++;
				}
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
		}
	}
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
			ColumnLODs.Add(Coord, TargetLOD);
			ColumnWorkQueue.Add(Coord);
			WorkQueueSet.Add(Coord);
		}
	}

	// Sort work queue: prioritize higher detail (lower LOD) then closer distance
	ColumnWorkQueue.Sort([this, PlayerChunk](const FIntPoint& A, const FIntPoint& B) {
		int32 LODA = ColumnLODs.FindRef(A);
		int32 LODB = ColumnLODs.FindRef(B);
		if (LODA != LODB) return LODA < LODB;

		float DistA = FVector2D::DistSquared(FVector2D(A), FVector2D(PlayerChunk));
		float DistB = FVector2D::DistSquared(FVector2D(B), FVector2D(PlayerChunk));
		return DistA < DistB;
	});
}

int32 AChunkWorldManager::CalculateLODForColumn(FIntPoint ColumnCoord, FIntPoint PlayerChunk) const
{
	// Chebyshev distance
	int32 Dist = FMath::Max(FMath::Abs(ColumnCoord.X - PlayerChunk.X), FMath::Abs(ColumnCoord.Y - PlayerChunk.Y));

	if (Dist <= LODBaseDistance)     return 0; // Full
	if (Dist <= LODBaseDistance * 2) return 1; // Half
	if (Dist <= LODBaseDistance * 3) return 2; // Quarter
	return 3;                                  // Eighth
}

void AChunkWorldManager::UnloadChunkColumn(FIntPoint Coord)
{
	// Remove all vertical chunks for this XY column via O(1) targeted lookups (Max Z bounded safely to 128)
	for (int32 Z = 0; Z < 128; ++Z)
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
		&FinishedTasksQueue
	))->StartBackgroundTask();
}
