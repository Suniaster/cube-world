#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

/** Bitmasks for neighboring voxels at chunk boundaries. */
USTRUCT(BlueprintType)
struct FVoxelNeighborMasks
{
	GENERATED_BODY()

	/** 
	 * For each axis (0=Z, 1=X, 2=Y) and direction (0=negative, 1=positive),
	 * an array of uint64s containing one bit per column.
	 * Total bits per mask = AxisSizes[Axis].
	 */
	TArray<uint64> NeighborBits[3][2];
};

/** Data for a 3D voxel grid. */
USTRUCT(BlueprintType)
struct FVoxelGrid3D
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIntVector Size;

	/** Voxel type grid (0 = None/Empty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<uint8> Grid;

	FVoxelGrid3D() : Size(FIntVector(0, 0, 0))
	{
	}

	FVoxelGrid3D(int32 X, int32 Y, int32 Z) : Size(FIntVector(X, Y, Z))
	{
		Grid.SetNumZeroed(X * Y * Z);
	}

	bool IsValidIndex(int32 X, int32 Y, int32 Z) const
	{
		return X >= 0 && X < Size.X && Y >= 0 && Y < Size.Y && Z >= 0 && Z < Size.Z;
	}

	int32 GetIndex(int32 X, int32 Y, int32 Z) const
	{
		return X + (Y * Size.X) + (Z * Size.X * Size.Y);
	}

	void SetVoxel(int32 X, int32 Y, int32 Z, uint8 BlockType)
	{
		if (IsValidIndex(X, Y, Z))
		{
			Grid[GetIndex(X, Y, Z)] = BlockType;
		}
	}

	uint8 GetVoxel(int32 X, int32 Y, int32 Z) const
	{
		if (IsValidIndex(X, Y, Z))
		{
			return Grid[GetIndex(X, Y, Z)];
		}
		return 0; // None
	}
};
