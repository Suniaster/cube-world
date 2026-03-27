#include "ChunkGenerationTask.h"
#include "VoxelTerrainNoise.h"

void FChunkGenerationTask::DoWork()
{
	FChunkGenerationResult Result;
	Result.ChunkCoord = ChunkCoord;
	Result.ZLayer = ZLayer;
	Result.bSuccess = true;

	const float ChunkWorldX = static_cast<float>(ChunkCoord.X) * ChunkSize * VoxelSize;
	const float ChunkWorldY = static_cast<float>(ChunkCoord.Y) * ChunkSize * VoxelSize;
	const int32 BiomeCount = Biomes.Num();
	const int32 ZBase = ZLayer * ChunkHeight;

	// 1. Build height map and blend info
	TArray<TArray<int32>> HeightMap;
	TArray<TArray<FBiomeBlendInfo>> BlendMap;
	HeightMap.SetNum(ChunkSize);
	BlendMap.SetNum(ChunkSize);
	bool bHasAnyBlocks = false;

	for (int32 X = 0; X < ChunkSize; ++X)
	{
		HeightMap[X].SetNum(ChunkSize);
		BlendMap[X].SetNum(ChunkSize);
		for (int32 Y = 0; Y < ChunkSize; ++Y)
		{
			float WorldX = ChunkWorldX + X * VoxelSize;
			float WorldY = ChunkWorldY + Y * VoxelSize;

			FBiomeBlendInfo Blend = FVoxelTerrainNoise::GetBiomeBlendAt(
				WorldX, WorldY, BiomeCellSize, Seed, BiomeCount, BlendWidth);
			BlendMap[X][Y] = Blend;

			int32 PrimaryIdx = FMath::Clamp(static_cast<int32>(Blend.PrimaryBiome) - 1, 0, BiomeCount - 1);
			float H1 = FVoxelTerrainNoise::GetHeightForBiomeFloat(WorldX, WorldY, Biomes[PrimaryIdx], Seed);

			float FinalHeight = H1;
			if (Blend.BlendAlpha > SMALL_NUMBER)
			{
				int32 SecondaryIdx = FMath::Clamp(static_cast<int32>(Blend.SecondaryBiome) - 1, 0, BiomeCount - 1);
				float H2 = FVoxelTerrainNoise::GetHeightForBiomeFloat(WorldX, WorldY, Biomes[SecondaryIdx], Seed);
				FinalHeight = FMath::Lerp(H1, H2, Blend.BlendAlpha);
			}

			int32 H = FMath::Max(FMath::RoundToInt32(FinalHeight), 1);
			HeightMap[X][Y] = H;

			if (H > ZBase)
			{
				bHasAnyBlocks = true;
			}
		}
	}

	Result.bHasAnyBlocks = bHasAnyBlocks;

	if (bHasAnyBlocks)
	{
		// 2. Build Voxel Grid
		FVoxelGrid3D Grid(ChunkSize, ChunkSize, ChunkHeight);
		for (int32 X = 0; X < ChunkSize; ++X)
		{
			for (int32 Y = 0; Y < ChunkSize; ++Y)
			{
				int32 WorldColumnHeight = HeightMap[X][Y];
				uint8 BlockType = static_cast<uint8>(BlendMap[X][Y].PrimaryBiome);

				for (int32 LocalZ = 0; LocalZ < ChunkHeight; ++LocalZ)
				{
					if (ZBase + LocalZ < WorldColumnHeight)
					{
						Grid.SetVoxel(X, Y, LocalZ, BlockType);
					}
				}
			}
		}

		// 3. Generate Mesh Data
		UVoxelObject::GenerateMeshData(Grid, VoxelSize,
			[this, &BlendMap, BiomeCount](uint8 BlockType, const FVector& Pos, const FVector& /*Normal*/) -> FColor
			{
				int32 GridX = FMath::Clamp(FMath::FloorToInt32(Pos.X / VoxelSize), 0, ChunkSize - 1);
				int32 GridY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / VoxelSize), 0, ChunkSize - 1);

				const FBiomeBlendInfo& Blend = BlendMap[GridX][GridY];
				int32 PriIdx = FMath::Clamp(static_cast<int32>(Blend.PrimaryBiome) - 1, 0, BiomeCount - 1);
				FLinearColor PrimaryColor(Biomes[PriIdx].Color);

				if (Blend.BlendAlpha > SMALL_NUMBER)
				{
					int32 SecIdx = FMath::Clamp(static_cast<int32>(Blend.SecondaryBiome) - 1, 0, BiomeCount - 1);
					FLinearColor SecondaryColor(Biomes[SecIdx].Color);
					return FMath::Lerp(PrimaryColor, SecondaryColor, Blend.BlendAlpha).ToFColor(true);
				}
				return PrimaryColor.ToFColor(true);
			},
			Result.MeshData);
	}

	if (ResultQueue)
	{
		ResultQueue->Enqueue(MoveTemp(Result));
	}
}
