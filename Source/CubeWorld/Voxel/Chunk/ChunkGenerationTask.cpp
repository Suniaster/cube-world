#include "ChunkGenerationTask.h"
#include "VoxelTerrainNoise.h"

void FChunkGenerationTask::DoWork()
{
	FChunkGenerationResult Result;
	Result.ChunkCoord = ChunkCoord;
	Result.ZLayer = ZLayer;
	Result.LODLevel = LODLevel;
	Result.bSuccess = true;

	// Symmetric LOD: every dimension is downsampled by the same factor.
	const int32 LODScale             = 1 << LODLevel;
	const int32 EffectiveChunkSize   = ChunkSize   / LODScale;
	const int32 EffectiveChunkHeight = ChunkHeight / LODScale;
	const float EffectiveVoxelSize   = VoxelSize   * static_cast<float>(LODScale);

	const float ChunkWorldX = static_cast<float>(ChunkCoord.X) * ChunkSize * VoxelSize;
	const float ChunkWorldY = static_cast<float>(ChunkCoord.Y) * ChunkSize * VoxelSize;
	const int32 BiomeCount  = Biomes.Num();
	const int32 ZBase       = ZLayer * ChunkHeight;

	// 1. Height and biome map at effective resolution
	TArray<TArray<int32>>           HeightMap;
	TArray<TArray<FBiomeWeightInfo>> BlendMap;
	HeightMap.SetNum(EffectiveChunkSize);
	BlendMap.SetNum(EffectiveChunkSize);
	bool bHasAnyBlocks = false;

	for (int32 X = 0; X < EffectiveChunkSize; ++X)
	{
		HeightMap[X].SetNum(EffectiveChunkSize);
		BlendMap[X].SetNum(EffectiveChunkSize);
		for (int32 Y = 0; Y < EffectiveChunkSize; ++Y)
		{
			// Sample at the center of the effective (larger) voxel
			const float SampleX = ChunkWorldX + (X + 0.5f) * EffectiveVoxelSize;
			const float SampleY = ChunkWorldY + (Y + 0.5f) * EffectiveVoxelSize;

			FBiomeWeightInfo Weights = FVoxelTerrainNoise::GetBiomeWeights(
				SampleX, SampleY, BiomeCellSize, Seed, BiomeCount, BlendWidth);

			float H = 0.0f;
			for (const auto& Pair : Weights.Weights)
			{
				int32 BIdx = FMath::Clamp(static_cast<int32>(Pair.Key) - 1, 0, BiomeCount - 1);
				float BiomeH = FVoxelTerrainNoise::GetHeightForBiomeFloat(SampleX, SampleY, Biomes[BIdx], Seed);
				H += BiomeH * Pair.Value;
			}

			HeightMap[X][Y] = FMath::Max(FMath::RoundToInt32(H), 1);
			BlendMap[X][Y] = Weights; // We'll store FBiomeWeightInfo instead of FBiomeBlendInfo

			if (HeightMap[X][Y] > ZBase)
				bHasAnyBlocks = true;
		}
	}

	Result.bHasAnyBlocks = bHasAnyBlocks;

	if (bHasAnyBlocks)
	{
		// 2. Voxel grid — each effective voxel spans LODScale full-res voxels
		FVoxelGrid3D Grid(EffectiveChunkSize, EffectiveChunkSize, EffectiveChunkHeight);
		for (int32 X = 0; X < EffectiveChunkSize; ++X)
		{
			for (int32 Y = 0; Y < EffectiveChunkSize; ++Y)
			{
				// Determine primary block type (highest weight)
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

				const uint8 BlockType = static_cast<uint8>(PrimaryBiome);
				for (int32 LocalZ = 0; LocalZ < EffectiveChunkHeight; ++LocalZ)
				{
					if (ZBase + LocalZ * LODScale < HeightMap[X][Y])
						Grid.SetVoxel(X, Y, LocalZ, BlockType);
				}
			}
		}

		// 3. Mesh — EffectiveVoxelSize covers XY and Z correctly
		UVoxelObject::GenerateMeshData(Grid, EffectiveVoxelSize,
			[this, &BlendMap, BiomeCount, EffectiveChunkSize, EffectiveVoxelSize](uint8 /*BlockType*/, const FVector& Pos, const FVector& /*Normal*/) -> FColor
			{
				int32 GX = FMath::Clamp(FMath::FloorToInt32(Pos.X / EffectiveVoxelSize), 0, EffectiveChunkSize - 1);
				int32 GY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / EffectiveVoxelSize), 0, EffectiveChunkSize - 1);

				const FBiomeWeightInfo& Weights = BlendMap[GX][GY];
				FLinearColor FinalColor(0, 0, 0, 0);

				for (const auto& Pair : Weights.Weights)
				{
					int32 BIdx = FMath::Clamp(static_cast<int32>(Pair.Key) - 1, 0, BiomeCount - 1);
					FinalColor += FLinearColor(Biomes[BIdx].Color) * Pair.Value;
				}

				return FinalColor.ToFColor(true);
			},
			Result.MeshData);
	}

	if (ResultQueue)
	{
		ResultQueue->Enqueue(MoveTemp(Result));
	}
}
