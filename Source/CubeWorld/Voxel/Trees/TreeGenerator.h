#pragma once

#include "CoreMinimal.h"
#include "../VoxelTypes.h"
#include "../VoxelObject.h"
#include "TreeGenerator.generated.h"

// Block types and colors are private to the tree generation system.
// They are baked into per-archetype mesh data and never written into the chunk voxel grid.
constexpr uint8 TREE_BLOCKTYPE_WOOD   = 10;
constexpr uint8 TREE_BLOCKTYPE_LEAVES = 11;

USTRUCT(BlueprintType)
struct FVoxelTreeParams
{
	GENERATED_BODY()

	/** Number of initial attraction points in the crown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	int32 AttractionPoints = 300;

	/** Radius of the foliage crown volume where points are placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float CrownRadius = 8.0f;

	/** Base height of the solid trunk before branching heavily. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float TrunkHeight = 6.0f;

	/** The length of individual branch segments generated per algorithm step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float BranchLength = 1.2f;

	/** If an attraction point is within this distance to a branch node, it is "reached" and removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float KillDistance = 2.0f;

	/** Maximum distance a branch node can be influenced by an attraction point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float AttractionDistance = 15.0f;

	/** Radius of leaves generated around the terminal nodes at the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	float FoliageRadius = 2.5f;

	/** Maximum steps the generation loop will run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Space Colonization")
	int32 MaxSteps = 100;

	/** Voxel radius of the trunk cylinder. 0 = single-voxel trunk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trunk")
	float TrunkRadius = 1.f;
};


USTRUCT(BlueprintType)
struct FVoxelTreeData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree")
	FVoxelGrid3D Grid;

	/** The voxel offset of the trunk root relative to the Grid's (0,0,0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree")
	FIntVector CenterOffset;

	/** Pre-built mesh data, ready to be uploaded to a ProceduralMeshComponent on the game thread. */
	TMap<uint8, FVoxelMeshData> BlockMeshes;
};

/** 
 * Utility struct for running the Space Colonization Algorithm (SCA). 
 * This generates 3D trees and outputs them as local FVoxelGrid3D structures.
 */
struct FTreeGenerator
{
public:
	/** Generates a single tree. Returns a VoxelGrid3D containing the generated blocks (centered near XY center). */
	static FVoxelTreeData GenerateTree(const FVoxelTreeParams& Params, int32 Seed);
};
