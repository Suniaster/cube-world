#include "VoxelMeshGenerator.h"

struct FGreedyQuad
{
	uint32 X, Y, W, H;
};

static TArray<FGreedyQuad> GreedyMeshBinaryPlane(TArray<uint64>& Data, int32 PlaneHeight)
{
	TArray<FGreedyQuad> GreedyQuads;
	for (int32 Row = 0; Row < Data.Num(); ++Row)
	{
		int32 Y = 0;
		while (Y < PlaneHeight)
		{
			// Find first solid bit
			uint64 Bits = Data[Row] >> Y;
			if (Bits == 0)
			{
				Y = PlaneHeight;
				continue;
			}

			// FFS (Find First Set)
			int32 SetBit = FMath::CountTrailingZeros64(Bits);
			Y += SetBit;

			if (Y >= PlaneHeight) break;

			// Count consecutive ones
			uint64 RemainingBits = Data[Row] >> Y;
			int32 H = 0;
			uint64 TempBits = RemainingBits;
			while (TempBits & 1)
			{
				H++;
				TempBits >>= 1;
				if (Y + H >= PlaneHeight) break;
			}

			uint64 HAsMask = (1ULL << H) - 1;
			uint64 Mask = HAsMask << Y;

			// Grow horizontally
			int32 W = 1;
			while (Row + W < Data.Num() && Row + W < PlaneHeight)
			{
				uint64 NextRowH = (Data[Row + W] >> Y) & HAsMask;
				if (NextRowH != HAsMask) break;

				// Clear the bits we're expanding into
				Data[Row + W] &= ~Mask;
				W++;
			}

			GreedyQuads.Add({ (uint32)Row, (uint32)Y, (uint32)W, (uint32)H });
			Y += H;
		}
	}
	return GreedyQuads;
}

void UVoxelMeshGenerator::GenerateMeshFromGrid(const FVoxelGrid3D& Grid, float VoxelSize, FVoxelMeshData& OutMeshData)
{
	OutMeshData.Clear();

	const FIntVector GridSize = Grid.Size;
	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0) return;

	// Limit to uint64's capacity (64 bits). 
	const int32 MaxBitSize = 64;
	FIntVector ClampedSize(FMath::Min(GridSize.X, MaxBitSize), FMath::Min(GridSize.Y, MaxBitSize), FMath::Min(GridSize.Z, MaxBitSize));

	// 1. Column Mask Generation
	TArray<uint64> AxisCols[3];
	AxisCols[0].SetNumZeroed(ClampedSize.X * ClampedSize.Y);
	AxisCols[1].SetNumZeroed(ClampedSize.Y * ClampedSize.Z);
	AxisCols[2].SetNumZeroed(ClampedSize.Z * ClampedSize.X);

	for (int32 Z = 0; Z < ClampedSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < ClampedSize.Y; ++Y)
		{
			for (int32 X = 0; X < ClampedSize.X; ++X)
			{
				if (Grid.GetVoxel(X, Y, Z))
				{
					AxisCols[0][X + Y * ClampedSize.X] |= (1ULL << Z);
					AxisCols[1][Y + Z * ClampedSize.Y] |= (1ULL << X);
					AxisCols[2][Z + X * ClampedSize.Z] |= (1ULL << Y);
				}
			}
		}
	}

	// 2. Face Culling
	TArray<uint64> ColFaceMasks[3][2];
	ColFaceMasks[0][0].SetNumZeroed(ClampedSize.X * ClampedSize.Y);
	ColFaceMasks[0][1].SetNumZeroed(ClampedSize.X * ClampedSize.Y);
	ColFaceMasks[1][0].SetNumZeroed(ClampedSize.Y * ClampedSize.Z);
	ColFaceMasks[1][1].SetNumZeroed(ClampedSize.Y * ClampedSize.Z);
	ColFaceMasks[2][0].SetNumZeroed(ClampedSize.Z * ClampedSize.X);
	ColFaceMasks[2][1].SetNumZeroed(ClampedSize.Z * ClampedSize.X);

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int32 ColCount = (Axis == 0 ? ClampedSize.X * ClampedSize.Y : (Axis == 1 ? ClampedSize.Y * ClampedSize.Z : ClampedSize.Z * ClampedSize.X));
		for (int32 i = 0; i < ColCount; ++i)
		{
			uint64 Col = AxisCols[Axis][i];
			ColFaceMasks[Axis][0][i] = Col & ~(Col << 1); // Negative side faces
			ColFaceMasks[Axis][1][i] = Col & ~(Col >> 1); // Positive side faces
		}
	}

	// 3. Greedy Meshing Planes
	TMap<uint32, TMap<int32, TArray<uint64>>> PlaneGroups[6];

	for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
	{
		int32 Axis = FaceIdx / 2;
		int32 Direction = FaceIdx % 2;

		int32 Dim1Index = (Axis == 0 ? 0 : (Axis == 1 ? 1 : 2));
		int32 Dim2Index = (Axis == 0 ? 1 : (Axis == 1 ? 2 : 0));
		int32 Dim1Size = ClampedSize[Dim1Index];
		int32 Dim2Size = ClampedSize[Dim2Index];
		int32 AxisSize = ClampedSize[Axis];

		for (int32 V2 = 0; V2 < Dim2Size; ++V2)
		{
			for (int32 V1 = 0; V1 < Dim1Size; ++V1)
			{
				uint64 Col = ColFaceMasks[Axis][Direction][V1 + V2 * Dim1Size];

				while (Col != 0)
				{
					int32 V3 = FMath::CountTrailingZeros64(Col);
					Col &= Col - 1; 

					FIntVector VoxelPos;
					if (Axis == 0)      VoxelPos = FIntVector(V1, V2, V3);
					else if (Axis == 1) VoxelPos = FIntVector(V3, V1, V2);
					else                VoxelPos = FIntVector(V2, V3, V1);

					uint32 ColorHash = 0;
					{
						FColor VoxelColor = (Grid.VoxelColors.Num() == Grid.Grid.Num()) 
							? Grid.GetVoxelColor(VoxelPos.X, VoxelPos.Y, VoxelPos.Z) 
							: Grid.FaceColors[FaceIdx];
						ColorHash = (uint32(VoxelColor.R) << 24) | (uint32(VoxelColor.G) << 16) | (uint32(VoxelColor.B) << 8) | uint32(VoxelColor.A);
					}

					TMap<int32, TArray<uint64>>& Layers = PlaneGroups[FaceIdx].FindOrAdd(ColorHash);
					TArray<uint64>& Plane = Layers.FindOrAdd(V3);
					if (Plane.Num() == 0) Plane.SetNumZeroed(Dim1Size);
					Plane[V1] |= (1ULL << V2);
				}
			}
		}
	}

	// 4. Generate Quads and Add to Mesh
	auto AddQuad = [&](const FGreedyQuad& Quad, int32 AxisPos, int32 FaceIdx, uint32 ColorHash)
	{
		int32 Axis = FaceIdx / 2;
		FColor BaseColor;
		BaseColor.R = (ColorHash >> 24) & 0xFF;
		BaseColor.G = (ColorHash >> 16) & 0xFF;
		BaseColor.B = (ColorHash >> 8) & 0xFF;
		BaseColor.A = ColorHash & 0xFF;

		FVector Verts[4];
		FVector Normal;
		
		auto GetPos = [&](int32 V1, int32 V2, int32 V3) -> FVector {
			if (Axis == 0)      return FVector(V1, V2, V3) * VoxelSize;
			else if (Axis == 1) return FVector(V3, V1, V2) * VoxelSize;
			return                     FVector(V2, V3, V1) * VoxelSize;
		};

		int32 V3 = AxisPos;
		if (FaceIdx % 2 != 0) V3 += 1; // Offset for positive faces

		Verts[0] = GetPos(Quad.X,          Quad.Y,          V3);
		Verts[1] = GetPos(Quad.X + Quad.W, Quad.Y,          V3);
		Verts[2] = GetPos(Quad.X + Quad.W, Quad.Y + Quad.H, V3);
		Verts[3] = GetPos(Quad.X,          Quad.Y + Quad.H, V3);

		FVector Normals[6] = {
			FVector(0, 0, -1), FVector(0, 0, 1),  // Z-, Z+
			FVector(-1, 0, 0), FVector(1, 0, 0),  // X-, X+
			FVector(0, -1, 0), FVector(0, 1, 0)   // Y-, Y+
		};
		Normal = Normals[FaceIdx];

		int32 BaseIdx = OutMeshData.Vertices.Num();
		for (int32 i = 0; i < 4; ++i)
		{
			OutMeshData.Vertices.Add(Verts[i]);
			OutMeshData.Normals.Add(Normal);
			OutMeshData.Colors.Add(FLinearColor(BaseColor));
		}

		// Triangles (Clockwise winding for all faces as seen from outside)
		OutMeshData.Triangles.Add(BaseIdx + 0);
		OutMeshData.Triangles.Add(BaseIdx + 2);
		OutMeshData.Triangles.Add(BaseIdx + 1);
		OutMeshData.Triangles.Add(BaseIdx + 0);
		OutMeshData.Triangles.Add(BaseIdx + 3);
		OutMeshData.Triangles.Add(BaseIdx + 2);
	};

	// Final Greedy Pass
	for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
	{
		for (auto& ColorGroup : PlaneGroups[FaceIdx])
		{
			uint32 ColorHash = ColorGroup.Key;
			for (auto& PosGroup : ColorGroup.Value)
			{
				int32 PosOnAxis = PosGroup.Key;
				TArray<uint64>& Plane = PosGroup.Value;
				
				int32 Dim2Index = (FaceIdx / 2 == 0 ? 1 : (FaceIdx / 2 == 1 ? 2 : 0));
				int32 PlaneHeight = ClampedSize[Dim2Index];

				TArray<FGreedyQuad> Quads = GreedyMeshBinaryPlane(Plane, PlaneHeight);
				for (const FGreedyQuad& Quad : Quads)
				{
					AddQuad(Quad, PosOnAxis, FaceIdx, ColorHash);
				}
			}
		}
	}
}
