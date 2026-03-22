#include "WorldChunk.h"
#include "VoxelTerrainNoise.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#endif

AWorldChunk::AWorldChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	RootComponent = TerrainMesh;

	// Enable collision so characters can walk on the terrain
	TerrainMesh->bUseComplexAsSimpleCollision = true;
}

// ── Color helper ────────────────────────────────────────────────────────────

FColor AWorldChunk::GetVoxelColor(float WorldX, float WorldY)
{
	// Use low-frequency Perlin noise for smooth, flowing color variation
	// across the entire terrain — no per-block boundaries
	float ColorNoise = FMath::PerlinNoise2D(FVector2D(WorldX * 0.0008f, WorldY * 0.0008f));
	// Map from [-1,1] to [0,1]
	float T = ColorNoise * 0.5f + 0.5f;

	// Blend between two muted greens — subtle, smooth variation
	// Values higher here because lit material naturally darkens with light angle
	FLinearColor DarkGreen(0.15f, 0.30f, 0.10f);
	FLinearColor LightGreen(0.22f, 0.40f, 0.14f);

	FLinearColor Result = FMath::Lerp(DarkGreen, LightGreen, T);
	return Result.ToFColor(true);
}

// ── Cube geometry helper ────────────────────────────────────────────────────

void AWorldChunk::AddCube(
	FVector Position,
	float Size,
	const TArray<TArray<int32>>& HeightMap,
	int32 LocalX, int32 LocalY, int32 Z,
	int32 ChunkSizeVal,
	FColor CubeColor,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FColor>& VertexColors)
{
	// Helper: check if a neighbor voxel is solid (blocks face rendering)
	auto IsSolid = [&](int32 NX, int32 NY, int32 NZ) -> bool
	{
		if (NZ < 0) return true; // below ground = solid
		if (NX < 0 || NX >= ChunkSizeVal || NY < 0 || NY >= ChunkSizeVal)
			return false; // chunk border = treat as air (could cross-check, but OK for now)
		return NZ < HeightMap[NX][NY];
	};

	// The 8 corners of a cube
	FVector V[8] = {
		Position + FVector(0,    0,    0),      // 0 - bottom-left-front
		Position + FVector(Size, 0,    0),      // 1 - bottom-right-front
		Position + FVector(Size, Size, 0),      // 2 - bottom-right-back
		Position + FVector(0,    Size, 0),      // 3 - bottom-left-back
		Position + FVector(0,    0,    Size),   // 4 - top-left-front
		Position + FVector(Size, 0,    Size),   // 5 - top-right-front
		Position + FVector(Size, Size, Size),   // 6 - top-right-back
		Position + FVector(0,    Size, Size),   // 7 - top-left-back
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

	// Neighbor offsets per face (dx, dy, dz)
	int32 NeighborOff[6][3] = {
		{ 0,  0,  1}, // Top
		{ 0,  0, -1}, // Bottom
		{ 0, -1,  0}, // Front
		{ 0,  1,  0}, // Back
		{-1,  0,  0}, // Left
		{ 1,  0,  0}, // Right
	};

	for (int32 F = 0; F < 6; ++F)
	{
		int32 NX = LocalX + NeighborOff[F][0];
		int32 NY = LocalY + NeighborOff[F][1];
		int32 NZ = Z      + NeighborOff[F][2];

		// Skip this face if the neighbor voxel is solid (hidden face)
		if (IsSolid(NX, NY, NZ))
			continue;

		int32 BaseIdx = Vertices.Num();

		for (int32 C = 0; C < 4; ++C)
		{
			Vertices.Add(V[Faces[F].I[C]]);
			Normals.Add(Faces[F].Normal);
			VertexColors.Add(CubeColor);
		}

		// Two triangles for the quad (clockwise winding for UE/DirectX)
		Triangles.Add(BaseIdx + 0);
		Triangles.Add(BaseIdx + 2);
		Triangles.Add(BaseIdx + 1);

		Triangles.Add(BaseIdx + 0);
		Triangles.Add(BaseIdx + 3);
		Triangles.Add(BaseIdx + 2);
	}
}

// ── Chunk generation ────────────────────────────────────────────────────────

void AWorldChunk::GenerateChunk(
	FIntPoint InChunkCoord,
	int32 InChunkSize,
	float InVoxelSize,
	float InFrequency,
	float InAmplitude,
	int32 InOctaves,
	float InPersistence,
	float InLacunarity,
	float InSeed)
{
	ChunkCoord = InChunkCoord;

	// World-space origin of this chunk
	float ChunkWorldX = static_cast<float>(InChunkCoord.X) * InChunkSize * InVoxelSize;
	float ChunkWorldY = static_cast<float>(InChunkCoord.Y) * InChunkSize * InVoxelSize;
	SetActorLocation(FVector(ChunkWorldX, ChunkWorldY, 0.0f));

	// Build height map for this chunk
	TArray<TArray<int32>> HeightMap;
	HeightMap.SetNum(InChunkSize);

	int32 MaxHeightInChunk = 1;
	for (int32 X = 0; X < InChunkSize; ++X)
	{
		HeightMap[X].SetNum(InChunkSize);
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			float WorldX = ChunkWorldX + X * InVoxelSize;
			float WorldY = ChunkWorldY + Y * InVoxelSize;

			int32 H = FVoxelTerrainNoise::GetHeight(
				WorldX, WorldY,
				InFrequency, InAmplitude, InOctaves,
				InPersistence, InLacunarity, InSeed);
			HeightMap[X][Y] = H;
			MaxHeightInChunk = FMath::Max(MaxHeightInChunk, H);
		}
	}

	// Build mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FColor> VertexColors;

	// Pre-allocate rough estimates
	int32 EstimatedCubes = InChunkSize * InChunkSize * (MaxHeightInChunk / 2 + 1);
	Vertices.Reserve(EstimatedCubes * 8);
	Triangles.Reserve(EstimatedCubes * 12);

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			int32 ColumnHeight = HeightMap[X][Y];

			for (int32 Z = 0; Z < ColumnHeight; ++Z)
			{
				FVector CubePos(
					X * InVoxelSize,
					Y * InVoxelSize,
					Z * InVoxelSize);

			float WorldX = ChunkWorldX + X * InVoxelSize;
				float WorldY = ChunkWorldY + Y * InVoxelSize;
				FColor Color = GetVoxelColor(WorldX, WorldY);

				AddCube(
					CubePos, InVoxelSize,
					HeightMap,
					X, Y, Z,
					InChunkSize,
					Color,
					Vertices, Triangles, Normals, VertexColors);
			}
		}
	}

	// Convert FColor array to FLinearColor for procedural mesh
	TArray<FLinearColor> LinearVertexColors;
	LinearVertexColors.Reserve(VertexColors.Num());
	for (const FColor& C : VertexColors)
	{
		LinearVertexColors.Add(FLinearColor(C));
	}

	// Create the procedural mesh section
	TArray<FVector2D> EmptyUVs;
	TArray<FProcMeshTangent> EmptyTangents;

	TerrainMesh->CreateMeshSection_LinearColor(
		0,           // Section index
		Vertices,
		Triangles,
		Normals,
		EmptyUVs,
		LinearVertexColors,
		EmptyTangents,
		true);       // Create collision

	// Create a proper lit material that reads vertex colors and responds to lights
	static UMaterialInterface* VoxelMat = nullptr;
	if (!VoxelMat)
	{
		// Try to load a user-created material first
		VoxelMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Materials/M_VoxelTerrain.M_VoxelTerrain"));

#if WITH_EDITOR
		if (!VoxelMat)
		{
			// Create a DefaultLit material with VertexColor → BaseColor at runtime
			UMaterial* NewMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_VoxelTerrain_Runtime"));

			UMaterialExpressionVertexColor* VCExpr = NewObject<UMaterialExpressionVertexColor>(NewMat);
			NewMat->GetExpressionCollection().AddExpression(VCExpr);

			if (UMaterialEditorOnlyData* EditorData = NewMat->GetEditorOnlyData())
			{
				EditorData->BaseColor.Expression = VCExpr;
				EditorData->BaseColor.OutputIndex = 0; // RGB
			}

			NewMat->PreEditChange(nullptr);
			NewMat->PostEditChange();

			VoxelMat = NewMat;
		}
#endif
	}
	if (VoxelMat)
	{
		TerrainMesh->SetMaterial(0, VoxelMat);
	}
}
