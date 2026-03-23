#include "VoxelMeshGenerator.h"

void UVoxelMeshGenerator::GenerateMeshFromGrid(const FVoxelGrid3D& Grid, float VoxelSize, FVoxelMeshData& OutMeshData)
{
	OutMeshData.Clear();

	for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
		{
			for (int32 X = 0; X < Grid.Size.X; ++X)
			{
				if (Grid.GetVoxel(X, Y, Z))
				{
					AddCube(Grid, X, Y, Z, VoxelSize, OutMeshData);
				}
			}
		}	
	}
}

void UVoxelMeshGenerator::AddCube(
	const FVoxelGrid3D& Grid,
	int32 X, int32 Y, int32 Z,
	float VoxelSize,
	FVoxelMeshData& MeshData)
{
	FVector Position(X * VoxelSize, Y * VoxelSize, Z * VoxelSize);

	// The 8 corners of a cube
	FVector V[8] = {
		Position + FVector(0,         0,         0),         // 0 - bottom-left-front
		Position + FVector(VoxelSize, 0,         0),         // 1 - bottom-right-front
		Position + FVector(VoxelSize, VoxelSize, 0),         // 2 - bottom-right-back
		Position + FVector(0,         VoxelSize, 0),         // 3 - bottom-left-back
		Position + FVector(0,         0,         VoxelSize), // 4 - top-left-front
		Position + FVector(VoxelSize, 0,         VoxelSize), // 5 - top-right-front
		Position + FVector(VoxelSize, VoxelSize, VoxelSize), // 6 - top-right-back
		Position + FVector(0,         VoxelSize, VoxelSize), // 7 - top-left-back
	};

	// Each face: 4 verts, 2 triangles, 1 normal
	struct FaceData { int32 I[4]; FVector Normal; };

	FaceData Faces[6] = {
		// Top    (+Z)
		{{4, 5, 6, 7}, FVector(0, 0, 1)},
		// Bottom (-Z)
		{{3, 2, 1, 0}, FVector(0, 0, -1)},
		// Front  (-Y)
		{{0, 1, 5, 4}, FVector(0, -1, 0)},
		// Back   (+Y)
		{{2, 3, 7, 6}, FVector(0, 1, 0)},
		// Left   (-X)
		{{3, 0, 4, 7}, FVector(-1, 0, 0)},
		// Right  (+X)
		{{1, 2, 6, 5}, FVector(1, 0, 0)},
	};

	// Neighbor offsets per face
	int32 NeighborOff[6][3] = {
		{ 0,  0,  1}, // Top
		{ 0,  0, -1}, // Bottom
		{ 0, -1,  0}, // Front
		{ 0,  1,  0}, // Back
		{-1,  0,  0}, // Left
		{ 1,  0,  0}, // Right
	};

	// Iterating faces 
	for (int32 F = 0; F < 6; ++F)
	{
		int32 NX = X + NeighborOff[F][0];
		int32 NY = Y + NeighborOff[F][1];
		int32 NZ = Z + NeighborOff[F][2];

		// Skip if neighbor is solid (face culling)
		if (Grid.GetVoxel(NX, NY, NZ))
		{
			continue;
		}

		int32 BaseIdx = MeshData.Vertices.Num();

		FLinearColor FaceColor;
		if (Grid.VoxelColors.Num() == Grid.Grid.Num())
		{
			FaceColor = FLinearColor(Grid.GetVoxelColor(X, Y, Z));
		}
		else
		{
			FaceColor = FLinearColor(Grid.FaceColors[F]);
		}

		for (int32 C = 0; C < 4; ++C)
		{
			MeshData.Vertices.Add(V[Faces[F].I[C]]);
			MeshData.Normals.Add(Faces[F].Normal);
			MeshData.Colors.Add(FaceColor);
		}

		// Clockwise winding
		MeshData.Triangles.Add(BaseIdx + 0);
		MeshData.Triangles.Add(BaseIdx + 2);
		MeshData.Triangles.Add(BaseIdx + 1);

		MeshData.Triangles.Add(BaseIdx + 0);
		MeshData.Triangles.Add(BaseIdx + 3);
		MeshData.Triangles.Add(BaseIdx + 2);
	}
}
