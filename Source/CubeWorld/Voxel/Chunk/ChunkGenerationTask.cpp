#include "ChunkGenerationTask.h"
#include "VoxelTerrainNoise.h"

void FChunkGenerationTask::DoWork()
{
	// Symmetric LOD: every dimension is downsampled by the same factor.
	const int32 LODScale             = 1 << LODLevel;
	const int32 EffectiveChunkSize   = ChunkSize   / LODScale;
	const int32 EffectiveChunkHeight = ChunkHeight / LODScale;
	const float EffectiveVoxelSize   = VoxelSize   * static_cast<float>(LODScale);

	const float ChunkWorldX = static_cast<float>(ChunkCoord.X) * ChunkSize * VoxelSize;
	const float ChunkWorldY = static_cast<float>(ChunkCoord.Y) * ChunkSize * VoxelSize;
	const int32 BiomeCount  = Biomes.Num();

	// Calculate 9 identical Worley points once per chunk using its center
	const float ChunkCenterX = ChunkWorldX + (ChunkSize * VoxelSize * 0.5f);
	const float ChunkCenterY = ChunkWorldY + (ChunkSize * VoxelSize * 0.5f);
	const FVoxelTerrainNoise::FCachedWorleyPoints CachedPoints = FVoxelTerrainNoise::GetCachedWorleyPoints(
		ChunkCenterX, ChunkCenterY, BiomeCellSize, Seed, BiomeCount);

	// ── LOD 3+ Fast Heightmap Path ──
	if (LODLevel >= 3)
	{
		if (GenerateLOD3Heightmap(ChunkWorldX, ChunkWorldY, CachedPoints))
		{
			return;
		}
	}

	// ── Macro-Grid Bilinear Interpolation (LOD 0-2 voxel path only) ──
	TArray<int32>  HeightMap;
	TArray<uint8>  BlockTypeMap;
	TArray<FColor> ColorMap;
	int32 AbsoluteMaxHeight = 0;

	GenerateBilinearInterpolation(ChunkWorldX, ChunkWorldY, EffectiveChunkSize, EffectiveVoxelSize,
		CachedPoints, HeightMap, BlockTypeMap, ColorMap, AbsoluteMaxHeight);

	// ── ZLayer Generation Dispatch ──
	GenerateVoxelLayers(HeightMap, BlockTypeMap, ColorMap, AbsoluteMaxHeight,
		EffectiveChunkSize, EffectiveChunkHeight, EffectiveVoxelSize, LODScale);
}

bool FChunkGenerationTask::GenerateLOD3Heightmap(float ChunkWorldX, float ChunkWorldY, const FVoxelTerrainNoise::FCachedWorleyPoints& CachedPoints)
{
	const int32 Res        = FMath::Max(HeightmapResolution, 1);
	const int32 VertWidth  = Res + 1;
	const float CellSize   = static_cast<float>(ChunkSize) * VoxelSize / static_cast<float>(Res);

	TArray<int32>  CoarseHeightMap;
	TArray<FColor> CoarseColorMap;
	CoarseHeightMap.SetNumUninitialized(VertWidth * VertWidth);
	CoarseColorMap.SetNumUninitialized(VertWidth * VertWidth);

	int32 MaxH = 0;
	for (int32 X = 0; X < VertWidth; ++X)
	{
		for (int32 Y = 0; Y < VertWidth; ++Y)
		{
			const float SampleX = ChunkWorldX + static_cast<float>(X) * CellSize;
			const float SampleY = ChunkWorldY + static_cast<float>(Y) * CellSize;

			uint8 PrimaryBiome = 0;
			FColor VoxelColor;
			const int32 H = FVoxelTerrainNoise::GetWeightedHeightForLocation(
				SampleX, SampleY, BiomeCellSize, Seed, Biomes, BlendWidth, CachedPoints, PrimaryBiome, VoxelColor);

			const int32 Idx      = X * VertWidth + Y;
			CoarseHeightMap[Idx] = H;
			CoarseColorMap[Idx]  = VoxelColor;
			if (H > MaxH) MaxH = H;
		}
	}

	FChunkGenerationResult Result;
	Result.ChunkCoord     = ChunkCoord;
	Result.ZLayer         = 0;
	Result.LODLevel       = LODLevel;
	Result.bSuccess       = true;
	Result.bHasAnyBlocks  = (MaxH > 0);
	Result.bIsHeightmap   = true;

	if (Result.bHasAnyBlocks)
	{
		UVoxelObject::GenerateHeightmapMeshData(CoarseHeightMap, CoarseColorMap, Res, Res, CellSize, VoxelSize, Result.MeshData);
	}

	if (ResultQueue)
	{
		ResultQueue->Enqueue(MoveTemp(Result));
	}
	return true;
}

void FChunkGenerationTask::GenerateBilinearInterpolation(
	float ChunkWorldX, float ChunkWorldY, int32 EffectiveChunkSize, float EffectiveVoxelSize,
	const FVoxelTerrainNoise::FCachedWorleyPoints& CachedPoints,
	TArray<int32>& OutHeightMap, TArray<uint8>& OutBlockTypeMap, TArray<FColor>& OutColorMap, int32& OutAbsoluteMaxHeight)
{
	const int32 InterpolationStep = 8;
	const int32 StepX = FMath::Min(InterpolationStep, EffectiveChunkSize);
	const int32 StepY = FMath::Min(InterpolationStep, EffectiveChunkSize);

	const int32 MacroNodesX     = (EffectiveChunkSize / StepX) + 1;
	const int32 MacroNodesY     = (EffectiveChunkSize / StepY) + 1;
	const int32 TotalMacroNodes = MacroNodesX * MacroNodesY;

	TArray<float>        MacroHeight;
	TArray<uint8>        MacroBiome;
	TArray<FLinearColor> MacroColor;
	MacroHeight.SetNumUninitialized(TotalMacroNodes);
	MacroBiome.SetNumUninitialized(TotalMacroNodes);
	MacroColor.SetNumUninitialized(TotalMacroNodes);

	for (int32 MX = 0; MX < MacroNodesX; ++MX)
	{
		for (int32 MY = 0; MY < MacroNodesY; ++MY)
		{
			const float SampleX = ChunkWorldX + (MX * StepX + 0.5f) * EffectiveVoxelSize;
			const float SampleY = ChunkWorldY + (MY * StepY + 0.5f) * EffectiveVoxelSize;

			uint8  PrimaryBiome = 0;
			FColor VoxelColor;
			const int32 H = FVoxelTerrainNoise::GetWeightedHeightForLocation(
				SampleX, SampleY, BiomeCellSize, Seed, Biomes, BlendWidth, CachedPoints, PrimaryBiome, VoxelColor);

			const int32 MIdx  = MX * MacroNodesY + MY;
			MacroHeight[MIdx] = static_cast<float>(H);
			MacroBiome[MIdx]  = PrimaryBiome;
			MacroColor[MIdx]  = VoxelColor.ReinterpretAsLinear();
		}
	}

	const int32 VertexSize = EffectiveChunkSize + 1;
	const int32 TotalCells = VertexSize * VertexSize;

	OutHeightMap.SetNumUninitialized(TotalCells);
	OutBlockTypeMap.SetNumUninitialized(TotalCells);
	OutColorMap.SetNumUninitialized(TotalCells);

	OutAbsoluteMaxHeight = 0;

	int32 ColIndex = 0;
	for (int32 X = 0; X < VertexSize; ++X)
	{
		const int32 MX0 = FMath::Min(X / StepX, MacroNodesX - 2);
		const int32 MX1 = MX0 + 1;
		const float TX  = static_cast<float>(X - MX0 * StepX) / static_cast<float>(StepX);

		for (int32 Y = 0; Y < VertexSize; ++Y, ++ColIndex)
		{
			const int32 MY0 = FMath::Min(Y / StepY, MacroNodesY - 2);
			const int32 MY1 = MY0 + 1;
			const float TY  = static_cast<float>(Y - MY0 * StepY) / static_cast<float>(StepY);

			const float H00 = MacroHeight[MX0 * MacroNodesY + MY0];
			const float H10 = MacroHeight[MX1 * MacroNodesY + MY0];
			const float H01 = MacroHeight[MX0 * MacroNodesY + MY1];
			const float H11 = MacroHeight[MX1 * MacroNodesY + MY1];
			const float H0  = H00 + (H10 - H00) * TX;
			const float H1  = H01 + (H11 - H01) * TX;
			const int32 FinalH = FMath::RoundToInt32(H0 + (H1 - H0) * TY);

			const int32 NearestMX  = (TX < 0.5f) ? MX0 : MX1;
			const int32 NearestMY  = (TY < 0.5f) ? MY0 : MY1;
			const uint8 FinalBiome = MacroBiome[NearestMX * MacroNodesY + NearestMY];

			const FLinearColor C00 = MacroColor[MX0 * MacroNodesY + MY0];
			const FLinearColor C10 = MacroColor[MX1 * MacroNodesY + MY0];
			const FLinearColor C01 = MacroColor[MX0 * MacroNodesY + MY1];
			const FLinearColor C11 = MacroColor[MX1 * MacroNodesY + MY1];
			const FLinearColor C0  = C00 + (C10 - C00) * TX;
			const FLinearColor C1  = C01 + (C11 - C01) * TX;

			OutHeightMap[ColIndex]     = FinalH;
			OutBlockTypeMap[ColIndex]  = FinalBiome;
			OutColorMap[ColIndex]      = (C0 + (C1 - C0) * TY).ToFColor(true);

			if (FinalH > OutAbsoluteMaxHeight) OutAbsoluteMaxHeight = FinalH;
		}
	}
}

void FChunkGenerationTask::GenerateVoxelLayers(
	const TArray<int32>& HeightMap, const TArray<uint8>& BlockTypeMap, const TArray<FColor>& ColorMap,
	int32 AbsoluteMaxHeight, int32 EffectiveChunkSize, int32 EffectiveChunkHeight, float EffectiveVoxelSize, int32 LODScale)
{
	const int32 EffectiveMaxHeight = (MaxHeightHint > 0) ? FMath::Max(AbsoluteMaxHeight, MaxHeightHint) : AbsoluteMaxHeight;
	int32 ActiveZLayers = FMath::CeilToInt32(static_cast<float>(EffectiveMaxHeight) / static_cast<float>(ChunkHeight));
	ActiveZLayers = FMath::Clamp(ActiveZLayers, 1, 100);

	const int32 VertexSize = EffectiveChunkSize + 1;
	const int32 TotalCells = VertexSize * VertexSize;

	for (int32 ZLayerIdx = 0; ZLayerIdx < ActiveZLayers; ++ZLayerIdx)
	{
		const int32 ZBase = ZLayerIdx * ChunkHeight;
		bool bHasAnyBlocks = false;

		for (int32 i = 0; i < TotalCells; ++i)
		{
			if (HeightMap[i] > ZBase)
			{
				bHasAnyBlocks = true;
				break;
			}
		}

		FChunkGenerationResult Result;
		Result.ChunkCoord      = ChunkCoord;
		Result.ZLayer          = ZLayerIdx;
		Result.LODLevel        = LODLevel;
		Result.ColumnMaxHeight = (ZLayerIdx == 0) ? AbsoluteMaxHeight : 0;
		Result.bSuccess        = true;
		Result.bHasAnyBlocks   = bHasAnyBlocks;

		if (bHasAnyBlocks)
		{
			FVoxelGrid3D Grid(EffectiveChunkSize, EffectiveChunkSize, EffectiveChunkHeight);
			for (int32 X = 0; X < EffectiveChunkSize; ++X)
			{
				for (int32 Y = 0; Y < EffectiveChunkSize; ++Y)
				{
					const int32 GridIdx   = X * VertexSize + Y;
					const uint8 BlockType = BlockTypeMap[GridIdx];
					const int32 ColHeight = HeightMap[GridIdx];

					const int32 LimitZ = FMath::Clamp((ColHeight - ZBase + LODScale - 1) / LODScale, 0, EffectiveChunkHeight);

					for (int32 LocalZ = 0; LocalZ < LimitZ; ++LocalZ)
					{
						Grid.SetVoxel(X, Y, LocalZ, BlockType);
					}
				}
			}

			UVoxelObject::GenerateMeshData(Grid, EffectiveVoxelSize,
				[&ColorMap, VertexSize, EffectiveVoxelSize](uint8 /*BlockType*/, const FVector& Pos, const FVector& /*Normal*/) -> FColor
				{
					const int32 GX = FMath::Clamp(FMath::FloorToInt32(Pos.X / EffectiveVoxelSize), 0, VertexSize - 2);
					const int32 GY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / EffectiveVoxelSize), 0, VertexSize - 2);
					return ColorMap[GX * VertexSize + GY];
				},
				Result.MeshData);
		}

		if (ResultQueue)
		{
			ResultQueue->Enqueue(MoveTemp(Result));
		}
	}
}
