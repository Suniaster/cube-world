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

void AWorldChunk::GenerateChunk(
	FIntPoint InChunkCoord,
	int32 InChunkSize,
	float InVoxelSize,
	float InBiomeCellSize,
	float InSeed,
	const TArray<FVoxelBiomeParams>& InBiomes,
	float InBlendWidth,
	UMaterialInterface* InMaterial)
{
	ChunkCoord = InChunkCoord;

	// World-space origin of this chunk
	float ChunkWorldX = static_cast<float>(InChunkCoord.X) * InChunkSize * InVoxelSize;
	float ChunkWorldY = static_cast<float>(InChunkCoord.Y) * InChunkSize * InVoxelSize;
	SetActorLocation(FVector(ChunkWorldX, ChunkWorldY, 0.0f));

	const int32 BiomeCount = InBiomes.Num();

	// 1. Build height map, biome map, and per-column blend info
	TArray<TArray<int32>>          HeightMap;
	TArray<TArray<EVoxelBiome>>    BiomeMap;
	TArray<TArray<FBiomeBlendInfo>> BlendMap;
	HeightMap.SetNum(InChunkSize);
	BiomeMap.SetNum(InChunkSize);
	BlendMap.SetNum(InChunkSize);
	int32 MaxHeight = 0;

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		HeightMap[X].SetNum(InChunkSize);
		BiomeMap[X].SetNum(InChunkSize);
		BlendMap[X].SetNum(InChunkSize);
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			float WorldX = ChunkWorldX + X * InVoxelSize;
			float WorldY = ChunkWorldY + Y * InVoxelSize;

			// Worley noise with blend info
			FBiomeBlendInfo Blend = FVoxelTerrainNoise::GetBiomeBlendAt(
				WorldX, WorldY, InBiomeCellSize, InSeed, BiomeCount, InBlendWidth);
			BlendMap[X][Y] = Blend;
			BiomeMap[X][Y] = Blend.PrimaryBiome;

			// Primary biome height
			int32 PrimaryIdx = FMath::Clamp(static_cast<int32>(Blend.PrimaryBiome) - 1, 0, BiomeCount - 1);
			float H1 = FVoxelTerrainNoise::GetHeightForBiomeFloat(
				WorldX, WorldY, InBiomes[PrimaryIdx], InSeed);

			float FinalHeight = H1;

			// Blend with secondary biome if in transition zone
			if (Blend.BlendAlpha > SMALL_NUMBER)
			{
				int32 SecondaryIdx = FMath::Clamp(static_cast<int32>(Blend.SecondaryBiome) - 1, 0, BiomeCount - 1);
				float H2 = FVoxelTerrainNoise::GetHeightForBiomeFloat(
					WorldX, WorldY, InBiomes[SecondaryIdx], InSeed);

				FinalHeight = FMath::Lerp(H1, H2, Blend.BlendAlpha);
			}

			int32 H = FMath::Max(FMath::RoundToInt32(FinalHeight), 1);
			HeightMap[X][Y] = H;
			MaxHeight = FMath::Max(MaxHeight, H);
		}
	}

	// 2. Build Voxel Grid – block type encodes the primary biome
	FVoxelGrid3D Grid(InChunkSize, InChunkSize, MaxHeight);

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			int32 ColumnHeight = HeightMap[X][Y];
			uint8 BlockType = static_cast<uint8>(BiomeMap[X][Y]);
			for (int32 Z = 0; Z < ColumnHeight; ++Z)
			{
				Grid.SetVoxel(X, Y, Z, BlockType);
			}
		}
	}

	// 3. Create and build the voxel object with blended biome vertex colors
	if (!VoxelObject)
	{
		VoxelObject = NewObject<UVoxelObject>(this);
	}

	// Capture blend/biome data for the color callback
	// The color callback receives positions in local space (grid coords * VoxelSize),
	// so we recover grid X,Y from the vertex position.
	VoxelObject->Build(Grid, InVoxelSize,
		[&InBiomes, &BlendMap, BiomeCount, InVoxelSize, InChunkSize]
		(uint8 BlockType, const FVector& Pos, const FVector& /*Normal*/) -> FColor
		{
			// Recover grid coordinates from local vertex position
			int32 GridX = FMath::Clamp(FMath::FloorToInt32(Pos.X / InVoxelSize), 0, InChunkSize - 1);
			int32 GridY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / InVoxelSize), 0, InChunkSize - 1);

			const FBiomeBlendInfo& Blend = BlendMap[GridX][GridY];

			int32 PriIdx = FMath::Clamp(static_cast<int32>(Blend.PrimaryBiome) - 1, 0, BiomeCount - 1);
			FLinearColor PrimaryColor(InBiomes[PriIdx].Color);

			if (Blend.BlendAlpha > SMALL_NUMBER)
			{
				int32 SecIdx = FMath::Clamp(static_cast<int32>(Blend.SecondaryBiome) - 1, 0, BiomeCount - 1);
				FLinearColor SecondaryColor(InBiomes[SecIdx].Color);
				FLinearColor Blended = FMath::Lerp(PrimaryColor, SecondaryColor, Blend.BlendAlpha);
				return Blended.ToFColor(true);
			}

			return PrimaryColor.ToFColor(true);
		});

	// 4. Create/Update Dynamic Material
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

	// 5. Spawn the mesh representation
	VoxelObject->Spawn(this, DynMaterial ? (UMaterialInterface*)DynMaterial : InMaterial);
}
