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

	// ── Process the Column Load Queue (Budgeted) ──
	int32 SpawnedThisFrame = 0;
	while (ColumnLoadQueue.Num() > 0 && SpawnedThisFrame < MaxChunksPerFrame)
	{
		FIntPoint Coord = ColumnLoadQueue[0];
		ColumnLoadQueue.RemoveAt(0);

		if (!LoadedColumns.Contains(Coord))
		{
			LoadChunkColumn(Coord);
			// Note: SpawnedThisFrame is now a task dispatch count here, 
			// but we'll still use it to budget the Tick.
			SpawnedThisFrame++;
		}
	}

	// ── Process Finished Tasks (Budgeted GPU Uploads) ──
	int32 UploadsThisFrame = 0;
	FChunkGenerationResult FinishedResult;
	while (FinishedTasksQueue.Dequeue(FinishedResult) && UploadsThisFrame < MaxChunksPerFrame)
	{
		FIntVector Key(FinishedResult.ChunkCoord.X, FinishedResult.ChunkCoord.Y, FinishedResult.ZLayer);
		InFlightTasks.Remove(Key);

		// If column was unloaded while task was running, discard
		if (!LoadedColumns.Contains(FinishedResult.ChunkCoord))
		{
			continue;
		}

		if (FinishedResult.bSuccess && FinishedResult.bHasAnyBlocks)
		{
			EnsureMaterial();
			UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;

			// Calculate world-space location for this chunk
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
				NewChunk->ApplyGeneratedMesh(Key, FinishedResult.MeshData, UseMat);
				LoadedChunks.Add(Key, NewChunk);
				UploadsThisFrame++;
			}
		}
		else
		{
			// Even if it has no blocks, it's considered "loaded" (just empty)
			// But we don't spawn an actor.
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

	// Remove no-longer-desired columns from the queue
	for (int32 i = ColumnLoadQueue.Num() - 1; i >= 0; --i)
	{
		if (!DesiredColumns.Contains(ColumnLoadQueue[i]))
		{
			ColumnLoadQueue.RemoveAt(i);
		}
	}

	// 3. Queue new columns
	for (FIntPoint Coord : DesiredColumns)
	{
		if (!LoadedColumns.Contains(Coord) && !ColumnLoadQueue.Contains(Coord))
		{
			ColumnLoadQueue.Add(Coord);
		}
	}

	// Sort queue by distance to player (load closest first)
	ColumnLoadQueue.Sort([PlayerChunk](const FIntPoint& A, const FIntPoint& B) {
		float DistA = FVector2D::DistSquared(FVector2D(A), FVector2D(PlayerChunk));
		float DistB = FVector2D::DistSquared(FVector2D(B), FVector2D(PlayerChunk));
		return DistA < DistB;
	});
}

void AChunkWorldManager::LoadChunkColumn(FIntPoint Coord)
{
	if (LoadedColumns.Contains(Coord)) return;

	EnsureMaterial();
	UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;

	// Compute how many vertical layers we might need from the max biome amplitude
	float MaxAmplitude = 1.0f;
	for (const FVoxelBiomeParams& B : Biomes)
	{
		MaxAmplitude = FMath::Max(MaxAmplitude, B.Amplitude);
	}
	int32 MaxZLayers = FMath::CeilToInt32(MaxAmplitude / static_cast<float>(ChunkHeight));
	MaxZLayers = FMath::Max(MaxZLayers, 1);

	for (int32 Z = 0; Z < MaxZLayers; ++Z)
	{
		FIntVector ChunkKey(Coord.X, Coord.Y, Z);
		if (InFlightTasks.Contains(ChunkKey)) continue;

		InFlightTasks.Add(ChunkKey);

		(new FAutoDeleteAsyncTask<FChunkGenerationTask>(
			Coord,
			Z,
			ChunkSize,
			ChunkHeight,
			VoxelSize,
			BiomeCellSize,
			Seed,
			Biomes,
			BiomeBlendWidth,
			&FinishedTasksQueue
		))->StartBackgroundTask();
	}

	LoadedColumns.Add(Coord);
}

void AChunkWorldManager::UnloadChunkColumn(FIntPoint Coord)
{
	// Remove all vertical chunks for this XY column
	TArray<FIntVector> ToRemove;
	for (auto& Pair : LoadedChunks)
	{
		if (Pair.Key.X == Coord.X && Pair.Key.Y == Coord.Y)
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const FIntVector& Key : ToRemove)
	{
		AWorldChunk** Found = LoadedChunks.Find(Key);
		if (Found && *Found)
		{
			(*Found)->Destroy();
		}
		LoadedChunks.Remove(Key);
		InFlightTasks.Remove(Key);
	}

	LoadedColumns.Remove(Coord);
}
