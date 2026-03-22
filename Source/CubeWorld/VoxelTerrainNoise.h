#pragma once

#include "CoreMinimal.h"

/**
 * Stateless utility for procedural terrain height generation.
 * Uses multi-octave Perlin noise (FBM) to create varied, natural-looking terrain.
 */
struct CUBEWORLD_API FVoxelTerrainNoise
{
	/**
	 * Computes the terrain height (in voxel-column count) at a given world XY.
	 *
	 * @param WorldX      X coordinate in world space
	 * @param WorldY      Y coordinate in world space
	 * @param Frequency   Base noise frequency  (lower = bigger features)
	 * @param Amplitude   Maximum height in voxel columns
	 * @param Octaves     Number of noise layers
	 * @param Persistence How much each octave's amplitude decreases (0-1)
	 * @param Lacunarity  How much each octave's frequency increases
	 * @param Seed        World seed offset
	 * @return Height in voxel columns (always >= 1)
	 */
	static int32 GetHeight(
		float WorldX,
		float WorldY,
		float Frequency,
		float Amplitude,
		int32 Octaves,
		float Persistence,
		float Lacunarity,
		float Seed);

	/**
	 * Raw FBM noise value in [0, 1] range at the given position.
	 */
	static float SampleNoise01(
		float X, float Y,
		float Frequency,
		int32 Octaves,
		float Persistence,
		float Lacunarity,
		float Seed);
};
