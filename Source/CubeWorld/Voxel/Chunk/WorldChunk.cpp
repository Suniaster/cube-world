#include "WorldChunk.h"
#include "VoxelTerrainNoise.h"
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

void AWorldChunk::GenerateChunk(
	FIntPoint InChunkCoord,
	int32 InChunkSize,
	float InVoxelSize,
	float InFrequency,
	float InAmplitude,
	int32 InOctaves,
	float InPersistence,
	float InLacunarity,
	float InSeed,
	UMaterialInterface* InMaterial)
{
	ChunkCoord = InChunkCoord;

	// World-space origin of this chunk
	float ChunkWorldX = static_cast<float>(InChunkCoord.X) * InChunkSize * InVoxelSize;
	float ChunkWorldY = static_cast<float>(InChunkCoord.Y) * InChunkSize * InVoxelSize;
	SetActorLocation(FVector(ChunkWorldX, ChunkWorldY, 0.0f));

	// 1. Build height map and find max height
	TArray<TArray<int32>> HeightMap;
	HeightMap.SetNum(InChunkSize);
	int32 MaxHeight = 0;

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		HeightMap[X].SetNum(InChunkSize);
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			float WorldX = ChunkWorldX + X * InVoxelSize;
			float WorldY = ChunkWorldY + Y * InVoxelSize;

			int32 H = FVoxelTerrainNoise::GetHeight(
				WorldX, WorldY,
				InFrequency, InAmplitude, InOctaves,
				InPersistence, InLacunarity, InSeed);
			HeightMap[X][Y] = H;
			MaxHeight = FMath::Max(MaxHeight, H);
		}
	}

	// 2. Build Voxel Grid
	FVoxelGrid3D Grid(InChunkSize, InChunkSize, MaxHeight);

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			int32 ColumnHeight = HeightMap[X][Y];
			for (int32 Z = 0; Z < ColumnHeight; ++Z)
			{
				Grid.SetVoxel(X, Y, Z, 1); // Block type 1 for terrain
			}
		}
	}

	// 3. Create and build the voxel object
	if (!VoxelObject)
	{
		VoxelObject = NewObject<UVoxelObject>(this);
	}

	// USE GENERIC BUILD: No vertex colors requested now, shader will handle it
	VoxelObject->Build(Grid, InVoxelSize);

	// 4. Create/Update Dynamic Material
	UMaterialInstanceDynamic* DynMaterial = nullptr;
	if (InMaterial)
	{
		// Try to reuse existing dynamic material if possible
		if (VoxelObject->GetMeshComponent())
		{
			DynMaterial = Cast<UMaterialInstanceDynamic>(VoxelObject->GetMeshComponent()->GetMaterial(0));
		}
		
		if (!DynMaterial || DynMaterial->Parent != InMaterial)
		{
			DynMaterial = UMaterialInstanceDynamic::Create(InMaterial, this);
		}

		DynMaterial->SetScalarParameterValue(TEXT("NoiseScale"), 0.0008f);
	}

	// 5. Spawn the mesh representation
	VoxelObject->Spawn(this, DynMaterial ? (UMaterialInterface*)DynMaterial : InMaterial);
}
