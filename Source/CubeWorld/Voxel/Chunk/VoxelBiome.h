#pragma once

#include "CoreMinimal.h"
#include "VoxelBiome.generated.h"

/** Available biome types in the world. */
UENUM(BlueprintType)
enum class EVoxelBiome : uint8
{
	None          = 0 UMETA(Hidden),
	SnowMountains = 1,
	ForestPlains  = 2,
	Desert        = 3,

	MAX
};

/** Noise parameters and visual data for a single biome. Editable in the Details panel. */
USTRUCT(BlueprintType)
struct FVoxelBiomeParams
{
	GENERATED_BODY()

	/** Display name for the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FString Name;

	/** Base noise frequency (lower = bigger, smoother features). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "0.00001"))
	float Frequency = 0.001f;

	/** Maximum terrain height in voxel columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "1"))
	float Amplitude = 10.0f;

	/** Number of noise octaves (more = more detail). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 1;

	/** How quickly each octave's amplitude falls off (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "0", ClampMax = "1"))
	float Persistence = 0.5f;

	/** How quickly each octave's frequency increases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "1"))
	float Lacunarity = 2.0f;

	/** Exponent applied to the [0,1] noise before scaling by Amplitude. >1 flattens lows, <1 flattens highs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Noise", meta = (ClampMin = "0.1", ClampMax = "8"))
	float PowerCurve = 1.0f;

	/** Block color (used for vertex coloring). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome|Visual")
	FColor Color = FColor::White;
};

/** Number of built-in biomes. */
inline constexpr int32 VOXEL_BIOME_COUNT = 3;

/** Result of a multi-biome lookup with weights for each biome. */
USTRUCT(BlueprintType)
struct FBiomeWeightInfo
{
	GENERATED_BODY()

	/** Map of biome type to its influence weight (0.0 to 1.0). */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel")
	TMap<EVoxelBiome, float> Weights;
};


/** Returns default biome configurations. Called once to initialize editor defaults. */
inline TArray<FVoxelBiomeParams> GetDefaultBiomeParams()
{
	TArray<FVoxelBiomeParams> Defaults;
	Defaults.SetNum(VOXEL_BIOME_COUNT);

	// SnowMountains – vast flat snow fields with rare towering peaks
	Defaults[0].Name        = TEXT("Snow Mountains");
	Defaults[0].Frequency   = 0.00001f;
	Defaults[0].Amplitude   = 6000.0f;
	Defaults[0].Octaves     = 4;
	Defaults[0].Persistence = 0.5f;
	Defaults[0].Lacunarity  = 2.0f;
	Defaults[0].PowerCurve  = 8.0f;
	Defaults[0].Color       = FColor(230, 240, 255);

	// ForestPlains – mostly flat with rare small mountains
	Defaults[1].Name        = TEXT("Forest Plains");
	Defaults[1].Frequency   = 0.00002f;
	Defaults[1].Amplitude   = 1000.0f;
	Defaults[1].Octaves     = 8;
	Defaults[1].Persistence = 0.3f;
	Defaults[1].Lacunarity  = 2.0f;
	Defaults[1].PowerCurve  = 8.0f;
	Defaults[1].Color       = FColor(34, 139, 34);

	// Desert – very flat with minimal variation
	Defaults[2].Name        = TEXT("Desert");
	Defaults[2].Frequency   = 0.0001f;
	Defaults[2].Amplitude   = 3.0f;
	Defaults[2].Octaves     = 1;
	Defaults[2].Persistence = 0.0f;
	Defaults[2].Lacunarity  = 2.0f;
	Defaults[2].PowerCurve  = 0.7f;
	Defaults[2].Color       = FColor(210, 180, 100);

	return Defaults;
}
