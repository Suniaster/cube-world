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
