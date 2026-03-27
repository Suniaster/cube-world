#pragma once

#include "CoreMinimal.h"
#include "VoxelBiome.h"


/**
 * Stateless utility for procedural terrain height generation.
 * Uses Worley noise for biome selection and multi-octave Perlin noise for height.
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

	/** Computes height as a float (for blending) using the given biome noise parameters. */
	static float GetHeightForBiomeFloat(
		float WorldX,
		float WorldY,
		const FVoxelBiomeParams& Params,
		float Seed);

	/** Computes height using the given biome noise parameters. */
	static int32 GetHeightForBiome(
		float WorldX,
		float WorldY,
		const FVoxelBiomeParams& Params,
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

	/**
	 * Worley (cellular) noise: finds the closest feature point in a cell grid.
	 * Returns the biome assigned to that feature point.
	 *
	 * @param WorldX     X coordinate in world space
	 * @param WorldY     Y coordinate in world space
	 * @param CellSize   World-space size of each Worley cell (controls biome scale)
	 * @param Seed       World seed
	 * @param BiomeCount Number of biomes to distribute across feature points
	 * @return The biome for this location
	 */
	static EVoxelBiome GetBiomeAt(
		float WorldX,
		float WorldY,
		float CellSize,
		float Seed,
		int32 BiomeCount);

	/**
	 * Worley noise with multi-biome weights: returns all biomes within the blend range
	 * and their weights. Prevents "pops" when the 2nd closest biome changes.
	 *
	 * @param WorldX     X coordinate in world space
	 * @param WorldY     Y coordinate in world space
	 * @param CellSize   World-space size of each Worley cell
	 * @param Seed       World seed
	 * @param BiomeCount Number of biomes
	 * @param BlendWidth Fraction of cell size over which blending occurs (0-1).
	 * @return FBiomeWeightInfo with a map of biomes and their weights.
	 */
	static FBiomeWeightInfo GetBiomeWeights(
		float WorldX,
		float WorldY,
		float CellSize,
		float Seed,
		int32 BiomeCount,
		float BlendWidth);

	/**
	 * Computes the final blended terrain height for a given world position.
	 * Encapsulates both biome lookup and height blending.
	 *
	 * @param OutWeights  Returns the biome weights used for this location (useful for coloring).
	 * @return Final height in voxel columns (clamped to >= 1).
	 */
	static int32 GetWeightedHeightForLocation(
		float WorldX,
		float WorldY,
		float BiomeCellSize,
		float Seed,
		const TArray<FVoxelBiomeParams>& Biomes,
		float BlendWidth,
		FBiomeWeightInfo& OutWeights);


private:
	/** Deterministic 2D hash → float in [0, 1). Used for Worley jitter/biome assignment. */
	static float Hash2D(int32 X, int32 Y, float Seed);
};
