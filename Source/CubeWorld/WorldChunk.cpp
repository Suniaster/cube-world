#include "WorldChunk.h"
#include "VoxelTerrainNoise.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelMeshGenerator.h"

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
	return DarkGreen.ToFColor(true);
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
	float InSeed,
	UMaterialInterface* InMaterial)
{
	ChunkCoord = InChunkCoord;

	// World-space origin of this chunk
	float ChunkWorldX = static_cast<float>(InChunkCoord.X) * InChunkSize * InVoxelSize;
	float ChunkWorldY = static_cast<float>(InChunkCoord.Y) * InChunkSize * InVoxelSize;
	SetActorLocation(FVector(ChunkWorldX, ChunkWorldY, 0.0f));

	// 1. Build height map and find max height
	TArray<TArray<int32>> HeightMap;
	HeightMap.SetNum(InChunkSize);
	int32 MaxHeight = 0;

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
			MaxHeight = FMath::Max(MaxHeight, H);
		}
	}

	// 2. Build Voxel Grid
	FVoxelGrid3D Grid(InChunkSize, InChunkSize, MaxHeight);

	for (int32 X = 0; X < InChunkSize; ++X)
	{
		for (int32 Y = 0; Y < InChunkSize; ++Y)
		{
			int32 ColumnHeight = HeightMap[X][Y];
			for (int32 Z = 0; Z < ColumnHeight; ++Z)
			{
				float WorldX = ChunkWorldX + X * InVoxelSize;
				float WorldY = ChunkWorldY + Y * InVoxelSize;
				FColor Color = GetVoxelColor(WorldX, WorldY);

				Grid.SetVoxel(X, Y, Z, true, Color);
			}
		}
	}

	// 3. Generate Mesh
	FVoxelMeshData MeshData;
	UVoxelMeshGenerator::GenerateMeshFromGrid(Grid, InVoxelSize, MeshData);

	// 4. Create the procedural mesh section
	TArray<FVector2D> EmptyUVs;
	TArray<FProcMeshTangent> EmptyTangents;

	TerrainMesh->CreateMeshSection_LinearColor(
		0,           // Section index
		MeshData.Vertices,
		MeshData.Triangles,
		MeshData.Normals,
		EmptyUVs,
		MeshData.Colors,
		EmptyTangents,
		true);       // Create collision

	if (InMaterial)
	{
		TerrainMesh->SetMaterial(0, InMaterial);
	}
}
