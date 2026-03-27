#include "VoxelTerrainNoise.h"

float FVoxelTerrainNoise::SampleNoise01(
	float X, float Y,
	float Frequency,
	int32 Octaves,
	float Persistence,
	float Lacunarity,
	float Seed)
{
	float Total     = 0.0f;
	float Amp       = 1.0f;
	float Freq      = Frequency;
	float MaxValue  = 0.0f; // for normalization

	for (int32 i = 0; i < Octaves; ++i)
	{
		// FMath::PerlinNoise2D returns values in roughly [-1, 1]
		float NoiseVal = FMath::PerlinNoise2D(FVector2D(
			(X + Seed) * Freq,
			(Y + Seed) * Freq));

		Total    += NoiseVal * Amp;
		MaxValue += Amp;
		Amp      *= Persistence;
		Freq     *= Lacunarity;
	}

	// Normalize to [0, 1]
	return (Total / MaxValue) * 0.5f + 0.5f;
}

int32 FVoxelTerrainNoise::GetHeight(
	float WorldX,
	float WorldY,
	float Frequency,
	float Amplitude,
	int32 Octaves,
	float Persistence,
	float Lacunarity,
	float Seed)
{
	float Noise01 = SampleNoise01(WorldX, WorldY, Frequency, Octaves, Persistence, Lacunarity, Seed);

	// Apply a power curve to make flat plains more common and peaks rarer
	Noise01 = FMath::Pow(Noise01, 1.2f);

	int32 Height = FMath::RoundToInt32(Noise01 * Amplitude);
	return FMath::Max(Height, 1); // always at least 1 block tall
}

// ── Biome-aware height ──────────────────────────────────────────────────────

float FVoxelTerrainNoise::GetHeightForBiomeFloat(
	float WorldX,
	float WorldY,
	const FVoxelBiomeParams& Params,
	float Seed)
{
	float Noise01 = SampleNoise01(
		WorldX, WorldY,
		Params.Frequency, Params.Octaves,
		Params.Persistence, Params.Lacunarity,
		Seed);

	Noise01 = FMath::Pow(Noise01, Params.PowerCurve);

	return FMath::Max(Noise01 * Params.Amplitude, 1.0f);
}

int32 FVoxelTerrainNoise::GetHeightForBiome(
	float WorldX,
	float WorldY,
	const FVoxelBiomeParams& Params,
	float Seed)
{
	return FMath::RoundToInt32(GetHeightForBiomeFloat(WorldX, WorldY, Params, Seed));
}

// ── Worley noise for biome selection ────────────────────────────────────────

float FVoxelTerrainNoise::Hash2D(int32 X, int32 Y, float Seed)
{
	// Combine coordinates and seed into a deterministic hash
	int32 H = X * 374761393 + Y * 668265263 + FMath::FloorToInt32(Seed) * 1274126177;
	H = (H ^ (H >> 13)) * 1103515245;
	H = H ^ (H >> 16);
	// Map to [0, 1)
	return static_cast<float>(static_cast<uint32>(H) & 0x7FFFFFFF) / 2147483648.0f;
}

EVoxelBiome FVoxelTerrainNoise::GetBiomeAt(
	float WorldX,
	float WorldY,
	float CellSize,
	float Seed,
	int32 BiomeCount)
{
	// Which cell does this point fall in?
	float CellX = WorldX / CellSize;
	float CellY = WorldY / CellSize;
	int32 CellIX = FMath::FloorToInt32(CellX);
	int32 CellIY = FMath::FloorToInt32(CellY);

	float MinDist = TNumericLimits<float>::Max();
	EVoxelBiome ClosestBiome = EVoxelBiome::ForestPlains; // fallback

	// Search 3x3 neighborhood of cells
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			int32 NX = CellIX + DX;
			int32 NY = CellIY + DY;

			// Feature point: cell corner + jitter in [0, 1)
			float JitterX = Hash2D(NX, NY, Seed);
			float JitterY = Hash2D(NX + 1000, NY + 1000, Seed);
			float FeatureX = static_cast<float>(NX) + JitterX;
			float FeatureY = static_cast<float>(NY) + JitterY;

			float Dist = FVector2D::DistSquared(
				FVector2D(CellX, CellY),
				FVector2D(FeatureX, FeatureY));

			if (Dist < MinDist)
			{
				MinDist = Dist;

				// Assign biome from the feature point's hash
				float BiomeHash = Hash2D(NX + 2000, NY + 2000, Seed);
				int32 BiomeIndex = FMath::FloorToInt32(BiomeHash * BiomeCount);
				BiomeIndex = FMath::Clamp(BiomeIndex, 0, BiomeCount - 1);
				ClosestBiome = static_cast<EVoxelBiome>(BiomeIndex + 1); // enum starts at 1
			}
		}
	}

	return ClosestBiome;
}

// ── Worley noise with biome blending ────────────────────────────────────────


FBiomeWeightInfo FVoxelTerrainNoise::GetBiomeWeights(
	float WorldX,
	float WorldY,
	float CellSize,
	float Seed,
	int32 BiomeCount,
	float BlendWidth)
{
	float CellX = WorldX / CellSize;
	float CellY = WorldY / CellSize;
	int32 CellIX = FMath::FloorToInt32(CellX);
	int32 CellIY = FMath::FloorToInt32(CellY);

	// First pass: find the absolute closest feature point distance (D1)
	float MinDistSq = TNumericLimits<float>::Max();
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			int32 NX = CellIX + DX;
			int32 NY = CellIY + DY;
			float JitterX = Hash2D(NX, NY, Seed);
			float JitterY = Hash2D(NX + 1000, NY + 1000, Seed);
			float DistSq = FVector2D::DistSquared(FVector2D(CellX, CellY), FVector2D(NX + JitterX, NY + JitterY));
			MinDistSq = FMath::Min(MinDistSq, DistSq);
		}
	}

	float D1 = FMath::Sqrt(MinDistSq);
	FBiomeWeightInfo Result;
	float TotalWeight = 0.0f;

	// Second pass: accumulate weights for all points within (D1 + BlendWidth)
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			int32 NX = CellIX + DX;
			int32 NY = CellIY + DY;
			float JitterX = Hash2D(NX, NY, Seed);
			float JitterY = Hash2D(NX + 1000, NY + 1000, Seed);
			float DistSq = FVector2D::DistSquared(FVector2D(CellX, CellY), FVector2D(NX + JitterX, NY + JitterY));
			float Di = FMath::Sqrt(DistSq);

			float Diff = Di - D1;
			if (Diff < BlendWidth)
			{
				float BiomeHash = Hash2D(NX + 2000, NY + 2000, Seed);
				int32 BiomeIdx = FMath::Clamp(FMath::FloorToInt32(BiomeHash * BiomeCount), 0, BiomeCount - 1);
				EVoxelBiome Biome = static_cast<EVoxelBiome>(BiomeIdx + 1);

				// Basic linear weight that goes to zero exactly at BlendWidth distance from D1
				float T = 1.0f - FMath::Clamp(Diff / (BlendWidth + SMALL_NUMBER), 0.0f, 1.0f);
				// Cubic smoothstep for a softer transition
				float Weight = T * T * (3.0f - 2.0f * T);

				float& ExistingWeight = Result.Weights.FindOrAdd(Biome);
				ExistingWeight += Weight;
				TotalWeight += Weight;
			}
		}
	}

	// Normalize
	if (TotalWeight > SMALL_NUMBER)
	{
		for (auto& Pair : Result.Weights)
		{
			Pair.Value /= TotalWeight;
		}
	}
	else
	{
		// Fallback (this shouldn't happen)
		Result.Weights.Add(EVoxelBiome::ForestPlains, 1.0f);
	}

	return Result;
}

int32 FVoxelTerrainNoise::GetWeightedHeightForLocation(
	float WorldX,
	float WorldY,
	float BiomeCellSize,
	float Seed,
	const TArray<FVoxelBiomeParams>& Biomes,
	float BlendWidth,
	FBiomeWeightInfo& OutWeights)
{
	OutWeights = GetBiomeWeights(WorldX, WorldY, BiomeCellSize, Seed, Biomes.Num(), BlendWidth);

	float H = 0.0f;
	for (const auto& Pair : OutWeights.Weights)
	{
		int32 BIdx = FMath::Clamp(static_cast<int32>(Pair.Key) - 1, 0, Biomes.Num() - 1);
		float BiomeH = GetHeightForBiomeFloat(WorldX, WorldY, Biomes[BIdx], Seed);
		H += BiomeH * Pair.Value;
	}

	return FMath::Max(FMath::RoundToInt32(H), 1);
}
