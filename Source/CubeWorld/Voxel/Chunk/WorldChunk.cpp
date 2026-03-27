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
	int32 InZLayer,
	int32 InChunkSize,
	int32 InChunkHeight,
	float InVoxelSize,
	float InBiomeCellSize,
	float InSeed,
	const TArray<FVoxelBiomeParams>& InBiomes,
	float InBlendWidth,
	UMaterialInterface* InMaterial)
{
	ChunkCoord = InChunkCoord;
	ZLayer = InZLayer;

	// World-space origin of this chunk (including vertical offset)
	float ChunkWorldX = static_cast<float>(InChunkCoord.X) * InChunkSize * InVoxelSize;
	float ChunkWorldY = static_cast<float>(InChunkCoord.Y) * InChunkSize * InVoxelSize;
	float ChunkWorldZ = static_cast<float>(InZLayer) * InChunkHeight * InVoxelSize;
	SetActorLocation(FVector(ChunkWorldX, ChunkWorldY, ChunkWorldZ));

	const int32 BiomeCount = InBiomes.Num();
	const int32 ZBase = InZLayer * InChunkHeight; // world Z index of this layer's bottom

	// 1. Build height map, biome map, and blend info per column
	TArray<TArray<int32>>           HeightMap;  // full world height per column
	TArray<TArray<FBiomeWeightInfo>> BlendMap;
	HeightMap.SetNum(InChunkSize);
	BlendMap.SetNum(InChunkSize);
	bool bHasAnyBlocks = false;

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		HeightMap[X].SetNum(InChunkSize);
		BlendMap[X].SetNum(InChunkSize);
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			float WorldX = ChunkWorldX + X * InVoxelSize;
			float WorldY = ChunkWorldY + Y * InVoxelSize;

			// Worley noise with blend info
			FBiomeWeightInfo Weights = FVoxelTerrainNoise::GetBiomeWeights(
				WorldX, WorldY, InBiomeCellSize, InSeed, BiomeCount, InBlendWidth);
			BlendMap[X][Y] = Weights;

			float FinalHeight = 0.0f;
			for (const auto& Pair : Weights.Weights)
			{
				int32 BIdx = FMath::Clamp(static_cast<int32>(Pair.Key) - 1, 0, BiomeCount - 1);
				float BiomeH = FVoxelTerrainNoise::GetHeightForBiomeFloat(WorldX, WorldY, InBiomes[BIdx], InSeed);
				FinalHeight += BiomeH * Pair.Value;
			}

			int32 H = FMath::Max(FMath::RoundToInt32(FinalHeight), 1);
			HeightMap[X][Y] = H;

			// Does any block in this column fall within our Z layer?
			if (H > ZBase)
			{
				bHasAnyBlocks = true;
			}
		}
	}

	// Early out: if no column reaches this Z layer, skip mesh generation entirely
	if (!bHasAnyBlocks)
	{
		return;
	}

	// 2. Build Voxel Grid for this Z layer only
	FVoxelGrid3D Grid(InChunkSize, InChunkSize, InChunkHeight);

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			int32 WorldColumnHeight = HeightMap[X][Y];
			
			// Find primary biome (highest weight)
			EVoxelBiome PrimaryBiome = EVoxelBiome::ForestPlains;
			float MaxWeight = -1.0f;
			for (const auto& Pair : BlendMap[X][Y].Weights)
			{
				if (Pair.Value > MaxWeight)
				{
					MaxWeight = Pair.Value;
					PrimaryBiome = Pair.Key;
				}
			}
			uint8 BlockType = static_cast<uint8>(PrimaryBiome);

			// Fill blocks within this layer's Z range
			for (int32 LocalZ = 0; LocalZ < InChunkHeight; ++LocalZ)
			{
				int32 WorldZ = ZBase + LocalZ;
				if (WorldZ < WorldColumnHeight)
				{
					Grid.SetVoxel(X, Y, LocalZ, BlockType);
				}
			}
		}
	}

	// 3. Create and build the voxel object with blended biome vertex colors
	if (!VoxelObject)
	{
		VoxelObject = NewObject<UVoxelObject>(this);
	}

	VoxelObject->Build(Grid, InVoxelSize,
		[&InBiomes, &BlendMap, BiomeCount, InVoxelSize, InChunkSize]
		(uint8 BlockType, const FVector& Pos, const FVector& /*Normal*/) -> FColor
		{
			int32 GridX = FMath::Clamp(FMath::FloorToInt32(Pos.X / InVoxelSize), 0, InChunkSize - 1);
			int32 GridY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / InVoxelSize), 0, InChunkSize - 1);

			const FBiomeWeightInfo& Weights = BlendMap[GridX][GridY];
			FLinearColor FinalColor(0, 0, 0, 0);

			for (const auto& Pair : Weights.Weights)
			{
				int32 BIdx = FMath::Clamp(static_cast<int32>(Pair.Key) - 1, 0, BiomeCount - 1);
				FinalColor += FLinearColor(InBiomes[BIdx].Color) * Pair.Value;
			}

			return FinalColor.ToFColor(true);
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

	VoxelObject->Spawn(this, DynMaterial ? (UMaterialInterface*)DynMaterial : InMaterial);

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
