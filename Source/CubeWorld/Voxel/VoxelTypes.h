#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

/**
 * Fixed-width multi-word bitmask for greedy meshing.
 * Allows chunk dimensions to exceed the 64-voxel uint64 limit.
 */
template<int32 NumWords>
struct TBitMask
{
	uint64 Words[NumWords];

	TBitMask()
	{
		for (int32 i = 0; i < NumWords; ++i) Words[i] = 0;
	}

	void SetBit(int32 Bit)
	{
		if (Bit >= 0 && Bit < NumWords * 64)
		{
			Words[Bit / 64] |= (1ULL << (Bit % 64));
		}
	}

	bool GetBit(int32 Bit) const
	{
		if (Bit >= 0 && Bit < NumWords * 64)
		{
			return (Words[Bit / 64] & (1ULL << (Bit % 64))) != 0;
		}
		return false;
	}

	void ClearBit(int32 Bit)
	{
		if (Bit >= 0 && Bit < NumWords * 64)
		{
			Words[Bit / 64] &= ~(1ULL << (Bit % 64));
		}
	}

	bool IsZero() const
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (Words[i] != 0) return false;
		}
		return true;
	}

	TBitMask operator|(const TBitMask& O) const
	{
		TBitMask Result;
		for (int32 i = 0; i < NumWords; ++i) Result.Words[i] = Words[i] | O.Words[i];
		return Result;
	}

	TBitMask operator&(const TBitMask& O) const
	{
		TBitMask Result;
		for (int32 i = 0; i < NumWords; ++i) Result.Words[i] = Words[i] & O.Words[i];
		return Result;
	}

	TBitMask operator~() const
	{
		TBitMask Result;
		for (int32 i = 0; i < NumWords; ++i) Result.Words[i] = ~Words[i];
		return Result;
	}

	TBitMask& operator|=(const TBitMask& O)
	{
		for (int32 i = 0; i < NumWords; ++i) Words[i] |= O.Words[i];
		return *this;
	}

	TBitMask& operator&=(const TBitMask& O)
	{
		for (int32 i = 0; i < NumWords; ++i) Words[i] &= O.Words[i];
		return *this;
	}

	bool operator==(const TBitMask& O) const
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (Words[i] != O.Words[i]) return false;
		}
		return true;
	}

	bool operator!=(const TBitMask& O) const
	{
		return !(*this == O);
	}

	bool operator!=(int32 Zero) const { return !IsZero(); }
	bool operator==(int32 Zero) const { return IsZero(); }


	/** Shift left by 1 (with inter-word carry). */
	TBitMask ShiftLeft1() const
	{
		TBitMask Result;
		uint64 Carry = 0;
		for (int32 i = 0; i < NumWords; ++i)
		{
			uint64 NextCarry = Words[i] >> 63;
			Result.Words[i] = (Words[i] << 1) | Carry;
			Carry = NextCarry;
		}
		return Result;
	}

	/** Shift right by 1 (with inter-word carry). */
	TBitMask ShiftRight1() const
	{
		TBitMask Result;
		uint64 Carry = 0;
		for (int32 i = NumWords - 1; i >= 0; --i)
		{
			uint64 NextCarry = Words[i] & 1;
			Result.Words[i] = (Words[i] >> 1) | (Carry << 63);
			Carry = NextCarry;
		}
		return Result;
	}

	/** OR a single bit into word 0 (for SNegBit boundary injection). */
	TBitMask OrLowBit(uint64 Bit) const
	{
		TBitMask Result = *this;
		Result.Words[0] |= Bit;
		return Result;
	}

	/** OR a single bit at position (AxisDim - 1) (for SPosBit boundary injection). */
	TBitMask OrHighBit(uint64 Bit, int32 AxisDim) const
	{
		TBitMask Result = *this;
		if (AxisDim > 0)
		{
			int32 TargetBit = AxisDim - 1;
			Result.Words[TargetBit / 64] |= (Bit << (TargetBit % 64));
		}
		return Result;
	}

	/** CountTrailingZeros: index of the first set bit across all words (-1 if zero). */
	int32 CTZ() const
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (Words[i] != 0)
			{
				return i * 64 + FMath::CountTrailingZeros64(Words[i]);
			}
		}
		return -1;
	}

	/** Clear lowest set bit (equivalent to Mask &= Mask - 1). */
	TBitMask ClearLowestBit() const
	{
		TBitMask Result = *this;
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (Result.Words[i] != 0)
			{
				Result.Words[i] &= (Result.Words[i] - 1);
				break;
			}
		}
		return Result;
	}

	/** Create a mask with the lowest H bits set. */
	static TBitMask MakeLowMask(int32 H)
	{
		TBitMask Result;
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (H <= 0) break;
			if (H >= 64)
			{
				Result.Words[i] = ~0ULL;
				H -= 64;
			}
			else
			{
				Result.Words[i] = (1ULL << H) - 1;
				H = 0;
			}
		}
		return Result;
	}

	/** Shift left by arbitrary amount. */
	TBitMask ShiftLeftBy(int32 Amount) const
	{
		if (Amount <= 0) return *this;
		if (Amount >= NumWords * 64) return TBitMask();

		TBitMask Result;
		int32 WordShift = Amount / 64;
		int32 BitShift = Amount % 64;

		if (BitShift == 0)
		{
			for (int32 i = WordShift; i < NumWords; ++i)
			{
				Result.Words[i] = Words[i - WordShift];
			}
		}
		else
		{
			for (int32 i = NumWords - 1; i >= WordShift; --i)
			{
				uint64 Val = Words[i - WordShift] << BitShift;
				if (i - WordShift - 1 >= 0)
				{
					Val |= Words[i - WordShift - 1] >> (64 - BitShift);
				}
				Result.Words[i] = Val;
			}
		}
		return Result;
	}

	/** Shift right by arbitrary amount. */
	TBitMask ShiftRightBy(int32 Amount) const
	{
		if (Amount <= 0) return *this;
		if (Amount >= NumWords * 64) return TBitMask();

		TBitMask Result;
		int32 WordShift = Amount / 64;
		int32 BitShift = Amount % 64;

		if (BitShift == 0)
		{
			for (int32 i = 0; i < NumWords - WordShift; ++i)
			{
				Result.Words[i] = Words[i + WordShift];
			}
		}
		else
		{
			for (int32 i = 0; i < NumWords - WordShift; ++i)
			{
				uint64 Val = Words[i + WordShift] >> BitShift;
				if (i + WordShift + 1 < NumWords)
				{
					Val |= Words[i + WordShift + 1] << (64 - BitShift);
				}
				Result.Words[i] = Val;
			}
		}
		return Result;
	}
};

using FBitMask128 = TBitMask<2>;
using FBitMask256 = TBitMask<4>;


constexpr uint8 BLOCKTYPE_WATER = 255;

constexpr FColor WATER_COLOR(50, 100, 200, 255);

/** Bitmasks for neighboring voxels at chunk boundaries. */
USTRUCT(BlueprintType)
struct FVoxelNeighborMasks
{
	GENERATED_BODY()

	/**
	 * Indexed by [Type][Axis][Dir] where:
	 *   Type: 0 = solid, 1 = water
	 *   Axis: 0 = Z,     1 = X,     2 = Y
	 *   Dir:  0 = negative, 1 = positive
	 * Total bits per entry = AxisSizes[Axis].
	 */
	TArray<uint64> NeighborBits[2][3][2];
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
