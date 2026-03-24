#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

/** Data for a 3D voxel grid. */
USTRUCT(BlueprintType)
struct FVoxelGrid3D
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIntVector Size;

	/** Voxel existence grid (true = solid, false = air). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<bool> Grid;

	/** Optional: Color for each specific voxel in the grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FColor> VoxelColors;

	FVoxelGrid3D() : Size(FIntVector(0, 0, 0))
	{
	}

	FVoxelGrid3D(int32 X, int32 Y, int32 Z) : Size(FIntVector(X, Y, Z))
	{
		Grid.SetNumZeroed(X * Y * Z);
		VoxelColors.SetNumZeroed(X * Y * Z);
	}

	bool IsValidIndex(int32 X, int32 Y, int32 Z) const
	{
		return X >= 0 && X < Size.X && Y >= 0 && Y < Size.Y && Z >= 0 && Z < Size.Z;
	}

	int32 GetIndex(int32 X, int32 Y, int32 Z) const
	{
		return X + (Y * Size.X) + (Z * Size.X * Size.Y);
	}

	void SetVoxel(int32 X, int32 Y, int32 Z, bool bSolid, FColor Color = FColor::White)
	{
		if (IsValidIndex(X, Y, Z))
		{
			int32 Idx = GetIndex(X, Y, Z);
			Grid[Idx] = bSolid;
			VoxelColors[Idx] = Color;
		}
	}

	bool GetVoxel(int32 X, int32 Y, int32 Z) const
	{
		if (IsValidIndex(X, Y, Z))
		{
			return Grid[GetIndex(X, Y, Z)];
		}
		return false;
	}

	FColor GetVoxelColor(int32 X, int32 Y, int32 Z) const
	{
		if (IsValidIndex(X, Y, Z))
		{
			return VoxelColors[GetIndex(X, Y, Z)];
		}
		return FColor::White;
	}
};
