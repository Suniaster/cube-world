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

	// ── Cached Biome Smoothing ────────────────────────────────────────────────
	
	/** A cached feature point representing a physical biome peak in a single cellular cell. */
	struct FWorleyFeaturePoint
	{
		float X;
		float Y;
		EVoxelBiome Biome;
	};

	/** A cached container of the 9 nearest cellular points for an entire chunk region. */
	struct FCachedWorleyPoints
	{
		TArray<FWorleyFeaturePoint, TInlineAllocator<9>> Points;
	};

	/**
	 * Computes all 9 nearest neighboring Worley feature points exactly once purely based on origin X/Y.
	 */
	static FCachedWorleyPoints GetCachedWorleyPoints(
		float WorldX,
		float WorldY,
		float CellSize,
		float Seed,
		int32 BiomeCount);

	/**
	 * Worley noise with multi-biome weights using only exact cached points instead of raw seeded recalculations.
	 * and their weights. Prevents "pops" when the 2nd closest biome changes.
	 *
	 * @param CellX        Voxel X position relative to cell size (WorldX / CellSize)
	 * @param CellY        Voxel Y position relative to cell size (WorldY / CellSize)
	 * @param CachedPoints Pre-calculated 3x3 Worley neighbor map for the entire chunk
	 * @param BlendWidth   Fraction of cell size over which blending occurs (0-1).
	 * @return FBiomeWeightInfo with a map of biomes and their weights.
	 */
	static FBiomeWeightInfo GetBiomeWeights(
		float CellX,
		float CellY,
		const FCachedWorleyPoints& CachedPoints,
		float BlendWidth);

	/**
	 * Computes the final blended terrain height for a given world position.
	 * Encapsulates both biome lookup and height blending.
	 *
	 * @param OutPrimaryBiome  Returns the dominant biome (for block generation).
	 * @param OutColor         Returns the pre-blended mesh vertex color.
	 * @param CachedPoints     Pre-calculated Worley points
	 * @return Final height in voxel columns (clamped to >= 1).
	 */
	static int32 GetWeightedHeightForLocation(
		float WorldX,
		float WorldY,
		float BiomeCellSize,
		float Seed,
		const TArray<FVoxelBiomeParams>& Biomes,
		float BlendWidth,
		const FCachedWorleyPoints& CachedPoints,
		uint8& OutPrimaryBiome,
		FColor& OutColor);


private:
	/** Deterministic 2D hash → float in [0, 1). Used for Worley jitter/biome assignment. */
	static float Hash2D(int32 X, int32 Y, float Seed);
};
