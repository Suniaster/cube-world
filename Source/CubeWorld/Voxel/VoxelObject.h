#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "VoxelTypes.h"

#include "VoxelObject.generated.h"

class UProceduralMeshComponent;

/** Struct to hold the generated mesh data for a ProceduralMeshComponent. */
USTRUCT(BlueprintType)
struct FVoxelMeshData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TArray<FVector> Vertices;

	UPROPERTY(BlueprintReadWrite)
	TArray<int32> Triangles;

	UPROPERTY(BlueprintReadWrite)
	TArray<FVector> Normals;

	TArray<FVector2D> UV0;

	UPROPERTY(BlueprintReadWrite)
	TArray<FLinearColor> Colors;

	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
		UV0.Empty();
		Colors.Empty();
	}
};

/**
 * UVoxelObject
 * Holds generated mesh data and handles spawning/updating its visual representation in the world.
 */
UCLASS(BlueprintType)
class CUBEWORLD_API UVoxelObject : public UObject
{
	GENERATED_BODY()

public:
	UVoxelObject();

	/** Thread-safe static function to generate mesh data from a voxel grid. */
	static void GenerateMeshData(
		const FVoxelGrid3D& Grid,
		float VoxelSize,
		TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)> ColorFunc,
		FVoxelMeshData& OutSolidMeshData,
		FVoxelMeshData& OutWaterMeshData,
		const FVoxelNeighborMasks* NeighborMasks = nullptr);

	/** Thread-safe static function to generate mesh data from a heightmap (LOD 3+). */
	static void GenerateHeightmapMeshData(
		const TArray<int32>& HeightMap,
		const TArray<FColor>& ColorMap,
		int32 GridSizeX,
		int32 GridSizeY,
		float EffectiveVoxelSize,
		float BaseVoxelSize,
		FVoxelMeshData& OutMeshData);

	/** Spawns or updates a procedural mesh component on the target actor. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	UProceduralMeshComponent* Spawn(AActor* Owner, UMaterialInterface* Material, UMaterialInterface* WaterMaterial, bool bCreateCollision = true);

	/** Returns the stored mesh data (const). */
	const FVoxelMeshData& GetMeshData() const { return MeshData; }

	/** Returns the stored mesh data (non-const). */
	FVoxelMeshData& GetMeshData() { return MeshData; }

	const FVoxelMeshData& GetWaterMeshData() const { return WaterMeshData; }
	FVoxelMeshData& GetWaterMeshData() { return WaterMeshData; }

	/** Returns the spawned mesh component. */
	UProceduralMeshComponent* GetMeshComponent() const { return MeshComponent; }

private:
	UPROPERTY()
	FVoxelMeshData MeshData;

	UPROPERTY()
	FVoxelMeshData WaterMeshData;

	UPROPERTY()
	UProceduralMeshComponent* MeshComponent;

	/** Internally updates a specific mesh section from mesh data. */
	void UpdateMeshSection(int32 SectionIdx, FVoxelMeshData& Data, UMaterialInterface* Material, bool bCreateCollision);
};
