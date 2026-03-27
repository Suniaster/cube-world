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

FBiomeBlendInfo FVoxelTerrainNoise::GetBiomeBlendAt(
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

	// Track the two closest feature points
	float Dist1 = TNumericLimits<float>::Max(); // closest
	float Dist2 = TNumericLimits<float>::Max(); // second closest
	int32 Biome1Idx = 0;
	int32 Biome2Idx = 0;

	// Search 3x3 neighborhood of cells
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			int32 NX = CellIX + DX;
			int32 NY = CellIY + DY;

			float JitterX = Hash2D(NX, NY, Seed);
			float JitterY = Hash2D(NX + 1000, NY + 1000, Seed);
			float FeatureX = static_cast<float>(NX) + JitterX;
			float FeatureY = static_cast<float>(NY) + JitterY;

			float Dist = FVector2D::DistSquared(
				FVector2D(CellX, CellY),
				FVector2D(FeatureX, FeatureY));

			float BiomeHash = Hash2D(NX + 2000, NY + 2000, Seed);
			int32 BiomeIdx = FMath::Clamp(FMath::FloorToInt32(BiomeHash * BiomeCount), 0, BiomeCount - 1);

			if (Dist < Dist1)
			{
				// Current closest becomes second closest
				Dist2 = Dist1;
				Biome2Idx = Biome1Idx;
				// New closest
				Dist1 = Dist;
				Biome1Idx = BiomeIdx;
			}
			else if (Dist < Dist2)
			{
				Dist2 = Dist;
				Biome2Idx = BiomeIdx;
			}
		}
	}

	FBiomeBlendInfo Result;
	Result.PrimaryBiome   = static_cast<EVoxelBiome>(Biome1Idx + 1);
	Result.SecondaryBiome = static_cast<EVoxelBiome>(Biome2Idx + 1);

	// Compute blend alpha: based on how close this point is to the border
	// between the two nearest feature points.
	// (d2 - d1) / (d1 + d2) ranges from 0 (on border) to ~1 (deep inside a biome).
	// We use sqrt of distances for more linear falloff.
	float D1 = FMath::Sqrt(Dist1);
	float D2 = FMath::Sqrt(Dist2);
	float BorderProximity = (D2 - D1) / (D1 + D2 + SMALL_NUMBER);

	// BlendWidth controls how wide the transition zone is.
	// When BorderProximity < BlendWidth, we're in the blend zone.
	if (BlendWidth > SMALL_NUMBER)
	{
		// Smoothstep from 0 (on border) to BlendWidth (fully inside primary)
		float T = FMath::Clamp(BorderProximity / BlendWidth, 0.0f, 1.0f);
		// Smoothstep for a nice non-linear falloff
		Result.BlendAlpha = (1.0f - T * T * (3.0f - 2.0f * T)) * 0.5f;
	}
	else
	{
		Result.BlendAlpha = 0.0f; // no blending
	}

	return Result;
}
