#include "VoxelObject.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"

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
			uint64 Bits = (Y < 64) ? (Data[Row] >> Y) : 0;
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
			int32 H = 0;
			uint64 TempBits = Data[Row] >> Y;
			while (TempBits & 1)
			{
				H++;
				TempBits >>= 1;
				if (Y + H >= PlaneHeight) break;
				if (Y + H >= 64) break; // uint64 limit
			}

			uint64 HAsMask = (H == 64) ? ~0ULL : ((1ULL << H) - 1);
			uint64 Mask = HAsMask << Y;

			// Grow horizontally
			int32 W = 1;
			while (Row + W < Data.Num())
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

UVoxelObject::UVoxelObject()
	: MeshComponent(nullptr)
{
}

void UVoxelObject::GenerateMeshData(const FVoxelGrid3D& Grid, float VoxelSize, TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)> ColorFunc, FVoxelMeshData& OutMeshData, const FVoxelNeighborMasks* NeighborMasks)
{
	OutMeshData.Clear();

	const FIntVector GridSize = Grid.Size;
	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0) return;

	// 1. Column Mask Generation
	TArray<uint64> AxisCols[3];
	const uint64 AXIS_Z = 0;
	const uint64 AXIS_X = 1;
	const uint64 AXIS_Y = 2;
	TArray<uint64> AxisSizes;
	AxisSizes.Add(GridSize.X * GridSize.Y); // Z axis 
	AxisSizes.Add(GridSize.Y * GridSize.Z); // X axis 
	AxisSizes.Add(GridSize.Z * GridSize.X); // Y axis 
	AxisCols[AXIS_Z].SetNumZeroed(AxisSizes[AXIS_Z]);
	AxisCols[AXIS_X].SetNumZeroed(AxisSizes[AXIS_X]);
	AxisCols[AXIS_Y].SetNumZeroed(AxisSizes[AXIS_Y]);

	for (uint64 Z = 0; Z < GridSize.Z; ++Z)
	{
		for (uint64 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (uint64 X = 0; X < GridSize.X; ++X)
			{
				if (Grid.GetVoxel(X, Y, Z) != 0) // New: Check for non-zero block type
				{
					AxisCols[AXIS_Z][X + Y * GridSize.X] |= (1ULL << Z);
					AxisCols[AXIS_X][Y + Z * GridSize.Y] |= (1ULL << X);
					AxisCols[AXIS_Y][Z + X * GridSize.Z] |= (1ULL << Y);
				}
			}
		}
	}

	// 2. Face Culling
	TArray<uint64> ColFaceMasks[3][2];
	ColFaceMasks[AXIS_Z][0].SetNumZeroed(AxisSizes[AXIS_Z]);
	ColFaceMasks[AXIS_Z][1].SetNumZeroed(AxisSizes[AXIS_Z]);
	ColFaceMasks[AXIS_X][0].SetNumZeroed(AxisSizes[AXIS_X]);
	ColFaceMasks[AXIS_X][1].SetNumZeroed(AxisSizes[AXIS_X]);
	ColFaceMasks[AXIS_Y][0].SetNumZeroed(AxisSizes[AXIS_Y]);
	ColFaceMasks[AXIS_Y][1].SetNumZeroed(AxisSizes[AXIS_Y]);

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 AxisDim = (Axis == 0 ? GridSize.Z : (Axis == 1 ? GridSize.X : GridSize.Y));
		
		for (int32 i = 0; i < AxisSizes[Axis]; ++i)
		{
			uint64 Col = AxisCols[Axis][i];
			uint64 NeighborNegBit = 0;
			uint64 NeighborPosBit = 0;

			if (NeighborMasks)
			{
				const TArray<uint64>& MaskNeg = NeighborMasks->NeighborBits[Axis][0];
				const TArray<uint64>& MaskPos = NeighborMasks->NeighborBits[Axis][1];

				if (MaskNeg.Num() > (i / 64))
				{
					NeighborNegBit = (MaskNeg[i / 64] & (1ULL << (i % 64))) ? 1ULL : 0ULL;
				}
				if (MaskPos.Num() > (i / 64))
				{
					NeighborPosBit = (MaskPos[i / 64] & (1ULL << (i % 64))) ? 1ULL : 0ULL;
				}
			}

			ColFaceMasks[Axis][0][i] = Col & ~(Col << 1 | NeighborNegBit);
			ColFaceMasks[Axis][1][i] = Col & ~(Col >> 1 | (NeighborPosBit << (AxisDim - 1)));
		}
	}

	// 3. Greedy Meshing Planes
	TMap<uint8, TMap<int32, TArray<uint64>>> PlaneGroups[6]; // Now maps BlockType -> (AxisPos -> PlaneData)

	for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
	{
		int32 Axis = FaceIdx / 2;
		int32 Direction = FaceIdx % 2;

		int32 Dim1Size = (Axis == AXIS_Z ? GridSize.X : (Axis == AXIS_X ? GridSize.Y : GridSize.Z));
		int32 Dim2Size = (Axis == AXIS_Z ? GridSize.Y : (Axis == AXIS_X ? GridSize.Z : GridSize.X));

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

					uint8 BlockType = Grid.GetVoxel(VoxelPos.X, VoxelPos.Y, VoxelPos.Z);
					
					TMap<int32, TArray<uint64>>& Layers = PlaneGroups[FaceIdx].FindOrAdd(BlockType);
					TArray<uint64>& Plane = Layers.FindOrAdd(V3);
					if (Plane.Num() == 0) Plane.SetNumZeroed(Dim1Size);
					Plane[V1] |= (1ULL << V2);
				}
			}
		}
	}

	// 4. Generate Quads and Add to Mesh
	auto AddQuad = [&](const FGreedyQuad& Quad, int32 AxisPos, int32 FaceIdx, uint8 BlockType)
	{
		int32 Axis = FaceIdx / 2;
		
		auto GetPos = [&](int32 V1, int32 V2, int32 V3) -> FVector {
			if (Axis == 0)      return FVector(V1, V2, V3) * VoxelSize;
			else if (Axis == 1) return FVector(V3, V1, V2) * VoxelSize;
			return                     FVector(V2, V3, V1) * VoxelSize;
		};

		int32 V3 = AxisPos;
		if (FaceIdx % 2 != 0) V3 += 1; // Offset for positive faces

		FVector Verts[4];
		Verts[0] = GetPos(Quad.X,          Quad.Y,          V3);
		Verts[1] = GetPos(Quad.X + Quad.W, Quad.Y,          V3);
		Verts[2] = GetPos(Quad.X + Quad.W, Quad.Y + Quad.H, V3);
		Verts[3] = GetPos(Quad.X,          Quad.Y + Quad.H, V3);

		FVector Normals[6] = {
			FVector(0, 0, -1), FVector(0, 0, 1),  // Z-, Z+
			FVector(-1, 0, 0), FVector(1, 0, 0),  // X-, X+
			FVector(0, -1, 0), FVector(0, 1, 0)   // Y-, Y+
		};
		FVector Normal = Normals[FaceIdx];

		int32 BaseIdx = OutMeshData.Vertices.Num();
		for (int32 i = 0; i < 4; ++i)
		{
			OutMeshData.Vertices.Add(Verts[i]);
			OutMeshData.Normals.Add(Normal);
			OutMeshData.Colors.Add(FLinearColor(ColorFunc(BlockType, Verts[i], Normal)));
		}

		if (FaceIdx % 2 == 0) // Negative faces
		{
			OutMeshData.Triangles.Add(BaseIdx + 0);
			OutMeshData.Triangles.Add(BaseIdx + 1);
			OutMeshData.Triangles.Add(BaseIdx + 2);
			OutMeshData.Triangles.Add(BaseIdx + 0);
			OutMeshData.Triangles.Add(BaseIdx + 2);
			OutMeshData.Triangles.Add(BaseIdx + 3);
		}
		else // Positive faces
		{
			OutMeshData.Triangles.Add(BaseIdx + 0);
			OutMeshData.Triangles.Add(BaseIdx + 2);
			OutMeshData.Triangles.Add(BaseIdx + 1);
			OutMeshData.Triangles.Add(BaseIdx + 0);
			OutMeshData.Triangles.Add(BaseIdx + 3);
			OutMeshData.Triangles.Add(BaseIdx + 2);
		}
	};

	for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
	{
		for (auto& TypeGroup : PlaneGroups[FaceIdx])
		{
			uint8 BlockType = TypeGroup.Key;
			for (auto& PosGroup : TypeGroup.Value)
			{
				int32 PosOnAxis = PosGroup.Key;
				TArray<uint64>& Plane = PosGroup.Value;
				
				const uint32 Axis = FaceIdx / 2;
				int32 PlaneHeight = (Axis == AXIS_Z ? GridSize.Y : (Axis == AXIS_X ? GridSize.Z : GridSize.X));

				TArray<FGreedyQuad> Quads = GreedyMeshBinaryPlane(Plane, PlaneHeight);
				for (const FGreedyQuad& Quad : Quads)
				{
					AddQuad(Quad, PosOnAxis, FaceIdx, BlockType);
				}
			}
		}
	}
}

void UVoxelObject::GenerateHeightmapMeshData(const TArray<int32>& HeightMap, const TArray<FColor>& ColorMap, int32 GridSizeX, int32 GridSizeY, float EffectiveVoxelSize, float BaseVoxelSize, FVoxelMeshData& OutMeshData)
{
	OutMeshData.Clear();

	if (GridSizeX <= 0 || GridSizeY <= 0 || HeightMap.Num() == 0) return;

	// Note: GridSizeX and GridSizeY are the count of *cells*.
	// The HeightMap must have (GridSizeX + 1) * (GridSizeY + 1) elements now for alignment.
	int32 VertexWidth = GridSizeX + 1;
	int32 VertexHeight = GridSizeY + 1;

	// 1. Generate Vertices and Colors (GridSize + 1 x GridSize + 1)
	for (int32 Y = 0; Y < VertexHeight; ++Y)
	{
		for (int32 X = 0; X < VertexWidth; ++X)
		{
			// Direct access to the (N+1) buffer
			int32 Idx = X * VertexHeight + Y;
			if (!HeightMap.IsValidIndex(Idx)) continue;

			float H = static_cast<float>(HeightMap[Idx]); 
			
			// Vertex Position: XY is scaled by LOD voxel size, Z is scaled by BASE voxel size.
			FVector Pos(X * EffectiveVoxelSize, Y * EffectiveVoxelSize, H * BaseVoxelSize);
			OutMeshData.Vertices.Add(Pos);

			// Colors
			OutMeshData.Colors.Add(FLinearColor(ColorMap[Idx]));
		}
	}

	// 2. Generate Triangles and Normals
	for (int32 Y = 0; Y < GridSizeY; ++Y)
	{
		for (int32 X = 0; X < GridSizeX; ++X)
		{
			int32 i0 = X + Y * VertexWidth;
			int32 i1 = (X + 1) + Y * VertexWidth;
			int32 i2 = (X + 1) + (Y + 1) * VertexWidth;
			int32 i3 = X + (Y + 1) * VertexWidth;

			// Triangle 1
			OutMeshData.Triangles.Add(i0);
			OutMeshData.Triangles.Add(i2);
			OutMeshData.Triangles.Add(i1);

			// Triangle 2
			OutMeshData.Triangles.Add(i0);
			OutMeshData.Triangles.Add(i3);
			OutMeshData.Triangles.Add(i2);
		}
	}

	// 3. Simple Normal Calculation (Upwards)
	OutMeshData.Normals.Init(FVector(0, 0, 1), OutMeshData.Vertices.Num());
}

UProceduralMeshComponent* UVoxelObject::Spawn(AActor* Owner, UMaterialInterface* Material, bool bCreateCollision)
{
	if (!Owner) return nullptr;

	if (!MeshComponent)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(Owner);
		MeshComponent->RegisterComponent();
		
		USceneComponent* Root = Owner->GetRootComponent();
		if (Root)
		{
			MeshComponent->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}
		else
		{
			Owner->SetRootComponent(MeshComponent);
		}
		
		MeshComponent->bUseComplexAsSimpleCollision = true;
		MeshComponent->bUseAsyncCooking = true; // Offload Physic/Chaos baking to background threads
	}

	TArray<FVector2D> EmptyUVs;
	TArray<FProcMeshTangent> EmptyTangents;

	MeshComponent->CreateMeshSection_LinearColor(
		0,
		MeshData.Vertices,
		MeshData.Triangles,
		MeshData.Normals,
		EmptyUVs,
		MeshData.Colors,
		EmptyTangents,
		bCreateCollision);

	// ── MEMORY OPTIMIZATION ──
	// ProceduralMeshComponent already copied the data. 
	// Clear our local copy to reclaim memory.
	MeshData.Clear();

	if (Material)
	{
		MeshComponent->SetMaterial(0, Material);
	}

	return MeshComponent;
}
