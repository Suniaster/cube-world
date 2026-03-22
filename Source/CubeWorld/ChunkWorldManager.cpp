#include "ChunkWorldManager.h"
#include "WorldChunk.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AChunkWorldManager::AChunkWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AChunkWorldManager::BeginPlay()
{
	Super::BeginPlay();

	// Force an initial chunk load around origin
	FIntPoint Origin(0, 0);
	UpdateChunksAroundPlayer(Origin);
}

void AChunkWorldManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Find the player's pawn
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	APawn* PlayerPawn = PC->GetPawn();
	if (!PlayerPawn) return;

	FIntPoint CurrentChunk = WorldToChunkCoord(PlayerPawn->GetActorLocation());

	// Only update when the player crosses a chunk boundary
	if (!bHasLastPlayerChunk || CurrentChunk != LastPlayerChunk)
	{
		LastPlayerChunk = CurrentChunk;
		bHasLastPlayerChunk = true;
		UpdateChunksAroundPlayer(CurrentChunk);
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

	// 3. Load new chunks
	for (FIntPoint Coord : DesiredChunks)
	{
		if (!LoadedChunks.Contains(Coord))
		{
			LoadChunk(Coord);
		}
	}
}

void AChunkWorldManager::LoadChunk(FIntPoint Coord)
{
	if (LoadedChunks.Contains(Coord)) return;

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
			Frequency,
			Amplitude,
			Octaves,
			Persistence,
			Lacunarity,
			Seed);

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
