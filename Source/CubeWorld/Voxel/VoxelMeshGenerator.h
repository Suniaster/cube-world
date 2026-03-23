#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VoxelTypes.h"
#include "VoxelMeshGenerator.generated.h"

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
 * Utility class to generate optimized voxel meshes.
 */
UCLASS()
class CUBEWORLD_API UVoxelMeshGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generates mesh data from a 3D voxel grid
	 * 
	 * @param Grid         The input voxel grid.
	 * @param VoxelSize    Size of each voxel cube.
	 * @param OutMeshData  The generated mesh arrays.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	static void GenerateMeshFromGrid(const FVoxelGrid3D& Grid, float VoxelSize, FVoxelMeshData& OutMeshData);

private:
	/** Adds a cube at the given grid position, culling hidden faces. */
	static void AddCube(
		const FVoxelGrid3D& Grid,
		int32 X, int32 Y, int32 Z,
		float VoxelSize,
		FVoxelMeshData& MeshData);
};
