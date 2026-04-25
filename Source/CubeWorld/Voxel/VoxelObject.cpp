#include "VoxelObject.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshDescription.h"

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

void UVoxelObject::GenerateMeshData(const FVoxelGrid3D& Grid, float VoxelSize, TFunctionRef<FColor(uint8 BlockType, const FVector& Pos, const FVector& Normal)> ColorFunc, TMap<uint8, FVoxelMeshData>& OutBlockMeshes, const FVoxelNeighborMasks* NeighborMasks)
{
	OutBlockMeshes.Empty();

	const FIntVector GridSize = Grid.Size;
	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0) return;

	// 1. Column Mask Generation
	TArray<uint64> SolidAxisCols[3];
	TArray<uint64> WaterAxisCols[3];
	const int32 AXIS_Z = 0;
	const int32 AXIS_X = 1;
	const int32 AXIS_Y = 2;
	TArray<uint64> AxisSizes;
	AxisSizes.Add(GridSize.X * GridSize.Y); // Z axis 
	AxisSizes.Add(GridSize.Y * GridSize.Z); // X axis 
	AxisSizes.Add(GridSize.Z * GridSize.X); // Y axis 
	
	for (int32 i = 0; i < 3; ++i)
	{
		SolidAxisCols[i].SetNumZeroed(AxisSizes[i]);
		WaterAxisCols[i].SetNumZeroed(AxisSizes[i]);
	}

	for (uint64 Z = 0; Z < GridSize.Z; ++Z)
	{
		for (uint64 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (uint64 X = 0; X < GridSize.X; ++X)
			{
				uint8 BlockType = Grid.GetVoxel(X, Y, Z);
				if (BlockType != 0)
				{
					TArray<uint64>* Cols = (BlockType == BLOCKTYPE_WATER) ? WaterAxisCols : SolidAxisCols;
					Cols[AXIS_Z][X + Y * GridSize.X] |= (1ULL << Z);
					Cols[AXIS_X][Y + Z * GridSize.Y] |= (1ULL << X);
					Cols[AXIS_Y][Z + X * GridSize.Z] |= (1ULL << Y);
				}
			}
		}
	}

	// 2. Face Culling
	TArray<uint64> ColFaceMasks[3][2];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		ColFaceMasks[Axis][0].SetNumZeroed(AxisSizes[Axis]);
		ColFaceMasks[Axis][1].SetNumZeroed(AxisSizes[Axis]);
		
		const int32 AxisDim = (Axis == 0 ? GridSize.Z : (Axis == 1 ? GridSize.X : GridSize.Y));
		
		for (int32 i = 0; i < AxisSizes[Axis]; ++i)
		{
			const uint64 SolidCol = SolidAxisCols[Axis][i];
			const uint64 WaterCol = WaterAxisCols[Axis][i];
			uint64 SNegBit = 0, SPosBit = 0, WNegBit = 0, WPosBit = 0;

			if (NeighborMasks)
			{
				const int32 MaskIdx = i / 64;
				const uint64 MaskBit = 1ULL << (i % 64);
				if (NeighborMasks->NeighborBits[0][Axis][0].IsValidIndex(MaskIdx))
				{
					SNegBit = (NeighborMasks->NeighborBits[0][Axis][0][MaskIdx] & MaskBit) ? 1ULL : 0ULL;
					SPosBit = (NeighborMasks->NeighborBits[0][Axis][1][MaskIdx] & MaskBit) ? 1ULL : 0ULL;
					WNegBit = (NeighborMasks->NeighborBits[1][Axis][0][MaskIdx] & MaskBit) ? 1ULL : 0ULL;
					WPosBit = (NeighborMasks->NeighborBits[1][Axis][1][MaskIdx] & MaskBit) ? 1ULL : 0ULL;
				}
			}

			// Solid culling: Solid faces are only culled by other solid blocks
			const uint64 SNegMask = SolidCol << 1 | SNegBit;
			const uint64 SPosMask = SolidCol >> 1 | (SPosBit << (AxisDim - 1));
			
			// Water culling: Water faces are culled by ANY block (solid or water)
			const uint64 AnyCol = SolidCol | WaterCol;
			const uint64 AnyNegMask = AnyCol << 1 | (SNegBit | WNegBit);
			const uint64 AnyPosMask = AnyCol >> 1 | ((SPosBit | WPosBit) << (AxisDim - 1));

			ColFaceMasks[Axis][0][i] = (SolidCol & ~SNegMask) | (WaterCol & ~AnyNegMask);
			ColFaceMasks[Axis][1][i] = (SolidCol & ~SPosMask) | (WaterCol & ~AnyPosMask);
		}
	}

	// 3. Greedy Meshing Planes
	TMap<uint8, TMap<int32, TArray<uint64>>> PlaneGroups[6]; // [face] -> BlockType -> (AxisPos -> PlaneData)

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
					
					TArray<uint64>& Plane = PlaneGroups[FaceIdx].FindOrAdd(BlockType).FindOrAdd(V3);
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
		FVoxelMeshData& TargetMesh = OutBlockMeshes.FindOrAdd(BlockType);
		
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

		FVector2D QuadUVs[4];
		QuadUVs[0] = FVector2D(Quad.X,          Quad.Y);
		QuadUVs[1] = FVector2D(Quad.X + Quad.W, Quad.Y);
		QuadUVs[2] = FVector2D(Quad.X + Quad.W, Quad.Y + Quad.H);
		QuadUVs[3] = FVector2D(Quad.X,          Quad.Y + Quad.H);

		int32 BaseIdx = TargetMesh.Vertices.Num();
		for (int32 i = 0; i < 4; ++i)
		{
			TargetMesh.Vertices.Add(Verts[i]);
			TargetMesh.Normals.Add(Normal);
			TargetMesh.UV0.Add(QuadUVs[i]);
			TargetMesh.Colors.Add(FLinearColor(ColorFunc(BlockType, Verts[i], Normal)));
		}

		if (FaceIdx % 2 == 0) // Negative faces
		{
			TargetMesh.Triangles.Add(BaseIdx + 0);
			TargetMesh.Triangles.Add(BaseIdx + 1);
			TargetMesh.Triangles.Add(BaseIdx + 2);
			TargetMesh.Triangles.Add(BaseIdx + 0);
			TargetMesh.Triangles.Add(BaseIdx + 2);
			TargetMesh.Triangles.Add(BaseIdx + 3);
		}
		else // Positive faces
		{
			TargetMesh.Triangles.Add(BaseIdx + 0);
			TargetMesh.Triangles.Add(BaseIdx + 2);
			TargetMesh.Triangles.Add(BaseIdx + 1);
			TargetMesh.Triangles.Add(BaseIdx + 0);
			TargetMesh.Triangles.Add(BaseIdx + 3);
			TargetMesh.Triangles.Add(BaseIdx + 2);
		}
	};

	for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
	{
		const uint32 Axis       = FaceIdx / 2;
		const int32 PlaneHeight = (Axis == AXIS_Z ? GridSize.Y : (Axis == AXIS_X ? GridSize.Z : GridSize.X));

		for (auto& [BlockType, PosMap] : PlaneGroups[FaceIdx])
		{
			for (auto& [PosOnAxis, Plane] : PosMap)
			{
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

UProceduralMeshComponent* UVoxelObject::Spawn(AActor* Owner, UMaterialInterface* Material, UMaterialInterface* WaterMaterial, bool bCreateCollision)
{
	if (!Owner) return nullptr;

	if (!MeshComponent)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(Owner);
		MeshComponent->RegisterComponent();
		
		USceneComponent* Root = Owner->GetRootComponent();
		if (Root) MeshComponent->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		else      Owner->SetRootComponent(MeshComponent);
		
		MeshComponent->bUseComplexAsSimpleCollision = true;
		MeshComponent->bUseAsyncCooking = true; // Offload Physic/Chaos baking to background threads
	}

	int32 SectionIdx = 0;
	int32 NumExistingSections = MeshComponent->GetNumSections();

	if (HeightmapData.Vertices.Num() > 0)
	{
		UpdateMeshSection(SectionIdx++, HeightmapData, Material, false);
	}

	for (auto& Pair : BlockMeshes)
	{
		uint8 BlockType = Pair.Key;
		FVoxelMeshData& Data = Pair.Value;
		
		UMaterialInterface* UseMat = (BlockType == BLOCKTYPE_WATER) ? WaterMaterial : Material;
		bool bCollision = bCreateCollision && (BlockType != BLOCKTYPE_WATER);
		
		UpdateMeshSection(SectionIdx++, Data, UseMat, bCollision);
	}

	for (int32 i = SectionIdx; i < NumExistingSections; ++i)
	{
		MeshComponent->ClearMeshSection(i);
	}

	return MeshComponent;
}

void UVoxelObject::UpdateMeshSection(int32 SectionIdx, FVoxelMeshData& Data, UMaterialInterface* Material, bool bCreateCollision)
{
	if (Data.Vertices.Num() > 0)
	{
		static const TArray<FProcMeshTangent> EmptyTangents;
		MeshComponent->CreateMeshSection_LinearColor(SectionIdx, Data.Vertices, Data.Triangles, Data.Normals, Data.UV0, Data.Colors, EmptyTangents, bCreateCollision);
		if (Material) MeshComponent->SetMaterial(SectionIdx, Material);
	}
	else
	{
		MeshComponent->ClearMeshSection(SectionIdx);
	}
	Data.Clear();
}

UStaticMesh* UVoxelObject::BakeToStaticMesh(const TMap<uint8, FVoxelMeshData>& BlockMeshes, UObject* Outer, FName Name)
{
	if (BlockMeshes.IsEmpty()) return nullptr;

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, Name, RF_Public | RF_Standalone);
	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid();

	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> PolygonGroupNames = Attributes.GetPolygonGroupMaterialSlotNames();

	int32 SectionIdx = 0;
	int32 TotalTriangles = 0;
	for (const auto& Pair : BlockMeshes)
	{
		const FVoxelMeshData& Data = Pair.Value;
		if (Data.Vertices.Num() == 0) continue;

		FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
		PolygonGroupNames[PolygonGroup] = FName(*FString::Printf(TEXT("Section_%d"), SectionIdx++));

		TArray<FVertexID> VertexIDs;
		VertexIDs.Reserve(Data.Vertices.Num());

		for (int32 i = 0; i < Data.Vertices.Num(); ++i)
		{
			FVertexID V = MeshDescription.CreateVertex();
			VertexPositions[V] = (FVector3f)Data.Vertices[i];
			VertexIDs.Add(V);
		}

		for (int32 i = 0; i < Data.Triangles.Num(); i += 3)
		{
			TArray<FVertexInstanceID> TriangleInstances;
			for (int32 j = 0; j < 3; ++j)
			{
				int32 VIdx = Data.Triangles[i + j];
				FVertexInstanceID VI = MeshDescription.CreateVertexInstance(VertexIDs[VIdx]);
				VertexInstanceNormals[VI] = (FVector3f)Data.Normals[VIdx];
				VertexInstanceUVs[VI] = (FVector2f)Data.UV0[VIdx];
				VertexInstanceColors[VI] = FVector4f(FLinearColor(Data.Colors[VIdx]));
				TriangleInstances.Add(VI);
			}
			MeshDescription.CreateTriangle(PolygonGroup, TriangleInstances);
			TotalTriangles++;
		}
	}

	UStaticMeshDescription* StaticMeshDesc = StaticMesh->CreateStaticMeshDescription();
	StaticMeshDesc->SetMeshDescription(MeshDescription);

	TArray<UStaticMeshDescription*> MeshDescriptions;
	MeshDescriptions.Add(StaticMeshDesc);
	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions);

	UE_LOG(LogTemp, Warning, TEXT("Voxel: Baked StaticMesh '%s' with %d triangles."), *Name.ToString(), TotalTriangles);

	return StaticMesh;
}
