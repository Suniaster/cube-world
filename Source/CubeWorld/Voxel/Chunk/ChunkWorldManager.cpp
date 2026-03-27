#include "ChunkWorldManager.h"
#include "WorldChunk.h"
#include "VoxelBiome.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Materials/Material.h"

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

	// ── Process the Load Queue (Budgeted) ──
	int32 SpawnedThisFrame = 0;
	while (ChunkLoadQueue.Num() > 0 && SpawnedThisFrame < MaxChunksPerFrame)
	{
		FIntPoint Coord = ChunkLoadQueue[0];
		ChunkLoadQueue.RemoveAt(0);

		if (!LoadedChunks.Contains(Coord))
		{
			LoadChunk(Coord);
			SpawnedThisFrame++;
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
	// 1. Determine which chunks should be loaded
	TSet<FIntPoint> DesiredChunks;
	for (int32 DX = -RenderDistance; DX <= RenderDistance; ++DX)
	{
		for (int32 DY = -RenderDistance; DY <= RenderDistance; ++DY)
		{
			DesiredChunks.Add(FIntPoint(PlayerChunk.X + DX, PlayerChunk.Y + DY));
		}
	}

	// 2. Unload chunks that are no longer needed
	TArray<FIntPoint> ToUnload;
	for (auto& Pair : LoadedChunks)
	{
		if (!DesiredChunks.Contains(Pair.Key))
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (FIntPoint Coord : ToUnload)
	{
		UnloadChunk(Coord);
	}

	// Remove no-longer-desired chunks from the queue
	for (int32 i = ChunkLoadQueue.Num() - 1; i >= 0; --i)
	{
		if (!DesiredChunks.Contains(ChunkLoadQueue[i]))
		{
			ChunkLoadQueue.RemoveAt(i);
		}
	}

	// 3. Queue new chunks
	for (FIntPoint Coord : DesiredChunks)
	{
		if (!LoadedChunks.Contains(Coord) && !ChunkLoadQueue.Contains(Coord))
		{
			ChunkLoadQueue.Add(Coord);
		}
	}

	// Sort queue by distance to player (load closest first)
	ChunkLoadQueue.Sort([PlayerChunk](const FIntPoint& A, const FIntPoint& B) {
		float DistA = FVector2D::DistSquared(FVector2D(A), FVector2D(PlayerChunk));
		float DistB = FVector2D::DistSquared(FVector2D(B), FVector2D(PlayerChunk));
		return DistA < DistB;
	});
}

void AChunkWorldManager::LoadChunk(FIntPoint Coord)
{
	if (LoadedChunks.Contains(Coord)) return;

	EnsureMaterial();
	UMaterialInterface* UseMat = TerrainMaterial ? TerrainMaterial : CachedRuntimeMaterial;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	AWorldChunk* NewChunk = GetWorld()->SpawnActor<AWorldChunk>(
		AWorldChunk::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);

	if (NewChunk)
	{
		NewChunk->GenerateChunk(
			Coord,
			ChunkSize,
			VoxelSize,
			BiomeCellSize,
			Seed,
			Biomes,
			BiomeBlendWidth,
			UseMat);

		LoadedChunks.Add(Coord, NewChunk);
	}
}

void AChunkWorldManager::UnloadChunk(FIntPoint Coord)
{
	AWorldChunk** Found = LoadedChunks.Find(Coord);
	if (Found && *Found)
	{
		(*Found)->Destroy();
	}
	LoadedChunks.Remove(Coord);
}
