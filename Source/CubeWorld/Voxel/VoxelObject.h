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

	/** Spawns or updates a procedural mesh component on the target actor. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	UProceduralMeshComponent* Spawn(AActor* Owner, UMaterialInterface* Material);

	/** Returns the stored mesh data. */
	const FVoxelMeshData& GetMeshData() const { return MeshData; }

private:
	UPROPERTY()
	FVoxelMeshData MeshData;

	UPROPERTY()
	UProceduralMeshComponent* MeshComponent;

	/** Internal mesh generation logic ported from VoxelMeshGenerator */
	void GenerateMeshFromGrid(const FVoxelGrid3D& Grid, float VoxelSize, TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)>* ColorFunc);
};
