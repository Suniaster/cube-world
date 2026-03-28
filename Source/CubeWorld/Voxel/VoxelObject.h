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

	UPROPERTY(BlueprintReadWrite)
	TArray<FLinearColor> Colors;

	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
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

	/** Generates and stores mesh data from a voxel grid with optional vertex coloring. */
	void Build(const FVoxelGrid3D& Grid, float VoxelSize);

	/** Generates and stores mesh data from a voxel grid with a custom vertex coloring callback. */
	void Build(const FVoxelGrid3D& Grid, float VoxelSize, TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)> ColorFunc);

	/** Thread-safe static function to generate mesh data from a voxel grid. */
	static void GenerateMeshData(
		const FVoxelGrid3D& Grid,
		float VoxelSize,
		TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)> ColorFunc,
		FVoxelMeshData& OutMeshData);

	/** Spawns or updates a procedural mesh component on the target actor. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	UProceduralMeshComponent* Spawn(AActor* Owner, UMaterialInterface* Material, bool bCreateCollision = true);

	/** Returns the stored mesh data (const). */
	const FVoxelMeshData& GetMeshData() const { return MeshData; }

	/** Returns the stored mesh data (non-const). */
	FVoxelMeshData& GetMeshData() { return MeshData; }

	/** Returns the spawned mesh component. */
	UProceduralMeshComponent* GetMeshComponent() const { return MeshComponent; }

private:
	UPROPERTY()
	FVoxelMeshData MeshData;

	UPROPERTY()
	UProceduralMeshComponent* MeshComponent;
};
