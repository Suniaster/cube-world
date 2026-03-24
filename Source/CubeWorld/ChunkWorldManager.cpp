#include "ChunkWorldManager.h"
#include "WorldChunk.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Materials/Material.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionNoise.h"
#endif

AChunkWorldManager::AChunkWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
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
		// Create a DefaultLit material that uses WorldPosition for coloring
		UMaterial* NewMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_VoxelTerrain_Runtime"));

		// 1. World Position
		UMaterialExpressionWorldPosition* WorldPosExpr = NewObject<UMaterialExpressionWorldPosition>(NewMat);
		NewMat->GetExpressionCollection().AddExpression(WorldPosExpr);

		// 2. Scale Parameter
		UMaterialExpressionScalarParameter* ScaleParam = NewObject<UMaterialExpressionScalarParameter>(NewMat);
		ScaleParam->ParameterName = TEXT("NoiseScale");
		ScaleParam->DefaultValue = 0.0008f;
		NewMat->GetExpressionCollection().AddExpression(ScaleParam);

		// 3. Scaled Position
		UMaterialExpressionMultiply* ScaledPos = NewObject<UMaterialExpressionMultiply>(NewMat);
		ScaledPos->A.Expression = WorldPosExpr;
		ScaledPos->B.Expression = ScaleParam;
		NewMat->GetExpressionCollection().AddExpression(ScaledPos);

		// 4. Noise (Standard noise outputs roughly -1 to 1)
		UMaterialExpressionNoise* NoiseExpr = NewObject<UMaterialExpressionNoise>(NewMat);
		NoiseExpr->Position.Expression = ScaledPos;
		NoiseExpr->Scale = 1.0f;
		NewMat->GetExpressionCollection().AddExpression(NoiseExpr);

		// 5. Shift Noise Range to 0 - 1
		// Noise * 0.5 + 0.5
		UMaterialExpressionConstant* HalfConst = NewObject<UMaterialExpressionConstant>(NewMat);
		HalfConst->R = 0.5f;
		NewMat->GetExpressionCollection().AddExpression(HalfConst);

		UMaterialExpressionMultiply* ShiftMult = NewObject<UMaterialExpressionMultiply>(NewMat);
		ShiftMult->A.Expression = NoiseExpr;
		ShiftMult->B.Expression = HalfConst;
		NewMat->GetExpressionCollection().AddExpression(ShiftMult);

		UMaterialExpressionAdd* RangeShiftedNoise = NewObject<UMaterialExpressionAdd>(NewMat);
		RangeShiftedNoise->A.Expression = ShiftMult;
		RangeShiftedNoise->B.Expression = HalfConst;
		NewMat->GetExpressionCollection().AddExpression(RangeShiftedNoise);

		// 6. Color Tones (Dark Green to Light Green)
		UMaterialExpressionVectorParameter* DarkGreen = NewObject<UMaterialExpressionVectorParameter>(NewMat);
		DarkGreen->ParameterName = TEXT("DarkColor");
		DarkGreen->DefaultValue = FLinearColor(0.02f, 0.08f, 0.02f); // Deep forest green
		NewMat->GetExpressionCollection().AddExpression(DarkGreen);

		UMaterialExpressionVectorParameter* LightGreen = NewObject<UMaterialExpressionVectorParameter>(NewMat);
		LightGreen->ParameterName = TEXT("LightColor");
		LightGreen->DefaultValue = FLinearColor(0.12f, 0.28f, 0.08f); // Vibrant grass green
		NewMat->GetExpressionCollection().AddExpression(LightGreen);

		// 7. Lerp Colors based on noise
		UMaterialExpressionLinearInterpolate* ColorLerp = NewObject<UMaterialExpressionLinearInterpolate>(NewMat);
		ColorLerp->A.Expression = DarkGreen;
		ColorLerp->B.Expression = LightGreen;
		ColorLerp->Alpha.Expression = RangeShiftedNoise;
		NewMat->GetExpressionCollection().AddExpression(ColorLerp);

		if (UMaterialEditorOnlyData* EditorData = NewMat->GetEditorOnlyData())
		{
			EditorData->BaseColor.Expression = ColorLerp;
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
			Frequency,
			Amplitude,
			Octaves,
			Persistence,
			Lacunarity,
			Seed,
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
