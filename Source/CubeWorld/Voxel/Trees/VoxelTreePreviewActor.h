#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreeGenerator.h"
#include "VoxelTreePreviewActor.generated.h"

class UVoxelObject;

/**
 * Editor preview actor for the tree generator.
 * Place this in any level — tweak Params or Seed in the Details panel
 * and the tree regenerates live without starting the game.
 */
UCLASS(HideCategories = (Cooking, LOD, Physics, Replication, Rendering, Input, Actor))
class CUBEWORLD_API AVoxelTreePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelTreePreviewActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree Preview")
	FVoxelTreeParams Params;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree Preview")
	int32 Seed = 42;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree Preview")
	UMaterialInterface* Material = nullptr;

	/** Must match AChunkWorldManager::VoxelSize (default 100 UU = 1m per voxel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree Preview")
	float VoxelSize = 100.0f;

private:
	void EnsureMaterial();
	void Regenerate();

	UPROPERTY()
	UVoxelObject* VoxelObject;
};
