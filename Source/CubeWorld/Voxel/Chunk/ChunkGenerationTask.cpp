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
		UVoxelObject::GenerateHeightmapMeshData(CoarseHeightMap, CoarseColorMap, Res, Res, CellSize, VoxelSize, Result.HeightmapData);
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

	// Expand macro nodes to cover [-Step, Size + Step]
	const int32 MacroNodesX     = (EffectiveChunkSize / StepX) + 1;
	const int32 MacroNodesY     = (EffectiveChunkSize / StepY) + 1;
	
	// We use an offset of 1 node (Step voxels) to research into negative space
	auto GetMacroIdx = [&](int32 MX, int32 MY) { return (MX + 1) * (MacroNodesY + 2) + (MY + 1); };

	TArray<float>        MacroHeight;
	TArray<uint8>        MacroBiome;
	TArray<FLinearColor> MacroColor;
	MacroHeight.SetNumUninitialized((MacroNodesX + 2) * (MacroNodesY + 2));
	MacroBiome.SetNumUninitialized((MacroNodesX + 2) * (MacroNodesY + 2));
	MacroColor.SetNumUninitialized((MacroNodesX + 2) * (MacroNodesY + 2));

	for (int32 MX = -1; MX <= MacroNodesX; ++MX)
	{
		for (int32 MY = -1; MY <= MacroNodesY; ++MY)
		{
			const float SampleX = ChunkWorldX + (MX * StepX + 0.5f) * EffectiveVoxelSize;
			const float SampleY = ChunkWorldY + (MY * StepY + 0.5f) * EffectiveVoxelSize;

			uint8  PrimaryBiome = 0;
			FColor VoxelColor;
			const int32 H = FVoxelTerrainNoise::GetWeightedHeightForLocation(
				SampleX, SampleY, BiomeCellSize, Seed, Biomes, BlendWidth, CachedPoints, PrimaryBiome, VoxelColor);

			const int32 MIdx  = GetMacroIdx(MX, MY);
			MacroHeight[MIdx] = static_cast<float>(H);
			MacroBiome[MIdx]  = PrimaryBiome;
			MacroColor[MIdx]  = VoxelColor.ReinterpretAsLinear();
		}
	}

	// Output map is (Size + 2) x (Size + 2) to cover X, Y from -1 to EffectiveChunkSize
	const int32 MapWidth   = EffectiveChunkSize + 2; 
	const int32 TotalCells = MapWidth * MapWidth;

	OutHeightMap.SetNumUninitialized(TotalCells);
	OutBlockTypeMap.SetNumUninitialized(TotalCells);
	OutColorMap.SetNumUninitialized(TotalCells);

	OutAbsoluteMaxHeight = 0;

	for (int32 X = -1; X <= EffectiveChunkSize; ++X)
	{
		const int32 MX0 = FMath::FloorToInt(static_cast<float>(X) / StepX);
		const int32 MX1 = MX0 + 1;
		const float TX  = static_cast<float>(X - MX0 * StepX) / static_cast<float>(StepX);

		for (int32 Y = -1; Y <= EffectiveChunkSize; ++Y)
		{
			const int32 MY0 = FMath::FloorToInt(static_cast<float>(Y) / StepY);
			const int32 MY1 = MY0 + 1;
			const float TY  = static_cast<float>(Y - MY0 * StepY) / static_cast<float>(StepY);

			const float H00 = MacroHeight[GetMacroIdx(MX0, MY0)];
			const float H10 = MacroHeight[GetMacroIdx(MX1, MY0)];
			const float H01 = MacroHeight[GetMacroIdx(MX0, MY1)];
			const float H11 = MacroHeight[GetMacroIdx(MX1, MY1)];
			const float H0  = H00 + (H10 - H00) * TX;
			const float H1  = H01 + (H11 - H01) * TX;
			const int32 FinalH = FMath::RoundToInt32(H0 + (H1 - H0) * TY);

			const int32 NearestMX  = (TX < 0.5f) ? MX0 : MX1;
			const int32 NearestMY  = (TY < 0.5f) ? MY0 : MY1;
			const uint8 FinalBiome = MacroBiome[GetMacroIdx(NearestMX, NearestMY)];

			const FLinearColor C00 = MacroColor[GetMacroIdx(MX0, MY0)];
			const FLinearColor C10 = MacroColor[GetMacroIdx(MX1, MY0)];
			const FLinearColor C01 = MacroColor[GetMacroIdx(MX0, MY1)];
			const FLinearColor C11 = MacroColor[GetMacroIdx(MX1, MY1)];
			const FLinearColor C0  = C00 + (C10 - C00) * TX;
			const FLinearColor C1  = C01 + (C11 - C01) * TX;

			const int32 ColIndex = (X + 1) * MapWidth + (Y + 1);
			OutHeightMap[ColIndex]     = FinalH;
			OutBlockTypeMap[ColIndex]  = FinalBiome;
			OutColorMap[ColIndex]      = (C0 + (C1 - C0) * TY).ToFColor(true);

			// Only track absolute max for internal columns (or include padding, doesn't hurt)
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

	const int32 MapWidth = EffectiveChunkSize + 2;

	for (int32 ZLayerIdx = 0; ZLayerIdx < ActiveZLayers; ++ZLayerIdx)
	{
		const int32 ZBase = ZLayerIdx * ChunkHeight;
		bool bHasAnyBlocks = false;

		// Check internal columns for blocks in this Z-layer
		for (int32 X = 0; X < EffectiveChunkSize; ++X)
		{
			for (int32 Y = 0; Y < EffectiveChunkSize; ++Y)
			{
				if (HeightMap[(X + 1) * MapWidth + (Y + 1)] > ZBase)
				{
					bHasAnyBlocks = true;
					goto FoundBlocks;
				}
			}
		}
		if (WaterLevel > ZBase)
		{
			bHasAnyBlocks = true;
		}
	FoundBlocks:

		FChunkGenerationResult Result;
		Result.ChunkCoord      = ChunkCoord;
		Result.ZLayer          = ZLayerIdx;
		Result.LODLevel        = LODLevel;
		Result.ColumnMaxHeight = (ZLayerIdx == 0) ? AbsoluteMaxHeight : 0;
		Result.bSuccess        = true;
		Result.bHasAnyBlocks   = bHasAnyBlocks;

		// 3. Compute feature placements (ZLayer 0 only — trees are independent instances)
		if (ZLayerIdx == 0 && LODLevel <= MaxTreeLOD)
		{
			FChunkPlacementContext PlacementContext{
				ChunkCoord,
				ChunkSize,
				VoxelSize,
				EffectiveChunkSize,
				EffectiveVoxelSize,
				static_cast<float>(ChunkCoord.X) * ChunkSize * VoxelSize,
				static_cast<float>(ChunkCoord.Y) * ChunkSize * VoxelSize,
				BiomeCellSize,
				Seed,
				Biomes,
				BlendWidth,
				WaterLevel,
				HeightMap,
				BlockTypeMap,
				MapWidth
			};

			for (const auto& Feature : Features)
			{
				if (Feature.IsValid())
				{
					Feature->ComputePlacements(PlacementContext, Result.FeaturePlacements);
				}
			}
		}

		if (bHasAnyBlocks)
		{
			FVoxelGrid3D Grid(EffectiveChunkSize, EffectiveChunkSize, EffectiveChunkHeight);
			
			// ── NEIGHBOR MASK CALCULATION ──
			FVoxelNeighborMasks NeighborMasks;
			int32 AxisSizes[3] = {
				EffectiveChunkSize * EffectiveChunkSize,			// Z-axis (X * Y)
				EffectiveChunkSize * EffectiveChunkHeight,			// X-axis (Y * Z)
				EffectiveChunkHeight * EffectiveChunkSize			// Y-axis (Z * X)
			};

			for (int32 Type = 0; Type < 2; ++Type)
			for (int32 Axis = 0; Axis < 3; ++Axis)
			for (int32 Dir  = 0; Dir  < 2; ++Dir)
			{
				NeighborMasks.NeighborBits[Type][Axis][Dir].SetNumZeroed((AxisSizes[Axis] + 63) / 64);
			}

			auto SetNeighborBit = [&](int32 Axis, int32 Dir, int32 BitIdx, bool bWater = false)
			{
				NeighborMasks.NeighborBits[bWater ? 1 : 0][Axis][Dir][BitIdx / 64] |= (1ULL << (BitIdx % 64));
			};

			// 1. Populate internal grid and Z-neighbor masks
			for (int32 X = 0; X < EffectiveChunkSize; ++X)
			{
				for (int32 Y = 0; Y < EffectiveChunkSize; ++Y)
				{
					const int32 MapIdx   = (X + 1) * MapWidth + (Y + 1);
					const uint8 BlockType = BlockTypeMap[MapIdx];
					const int32 ColHeight = HeightMap[MapIdx];

					const int32 LimitZ = FMath::Clamp((ColHeight - ZBase + LODScale - 1) / LODScale, 0, EffectiveChunkHeight);
					const int32 WaterLimitZ = FMath::Clamp((WaterLevel - ZBase + LODScale - 1) / LODScale, 0, EffectiveChunkHeight);

					for (int32 LocalZ = 0; LocalZ < LimitZ; ++LocalZ)
					{
						Grid.SetVoxel(X, Y, LocalZ, BlockType);
					}
					for (int32 LocalZ = LimitZ; LocalZ < WaterLimitZ; ++LocalZ)
					{
						Grid.SetVoxel(X, Y, LocalZ, BLOCKTYPE_WATER);
					}

					// Z-neighbor bits: check if block exists at ZBase-1 (bottom) or ZBase + ChunkHeight (top)
					const int32 BitIdx = X + Y * EffectiveChunkSize;
					if (ColHeight >= ZBase) SetNeighborBit(0, 0, BitIdx);
					if (WaterLevel >= ZBase) SetNeighborBit(0, 0, BitIdx, true);
					if (ColHeight >= ZBase + (EffectiveChunkHeight + 1) * LODScale) SetNeighborBit(0, 1, BitIdx);
					if (WaterLevel >= ZBase + (EffectiveChunkHeight + 1) * LODScale) SetNeighborBit(0, 1, BitIdx, true);
				}
			}

			// 2. Populate X/Y boundary neighbor masks (X=-1, X=Size, Y=-1, Y=Size)
			for (int32 i = 0; i < EffectiveChunkSize; ++i)
			{
				const int32 H_NegX = HeightMap[0 * MapWidth + (i + 1)];
				const int32 H_PosX = HeightMap[(EffectiveChunkSize + 1) * MapWidth + (i + 1)];
				const int32 H_NegY = HeightMap[(i + 1) * MapWidth + 0];
				const int32 H_PosY = HeightMap[(i + 1) * MapWidth + (EffectiveChunkSize + 1)];

				for (int32 LocalZ = 0; LocalZ < EffectiveChunkHeight; ++LocalZ)
				{
					const int32 Threshold = ZBase + (LocalZ * LODScale) + 1;
					
					// X neighbors (Index: Y + Z * Size)
					const int32 XBitIdx = i + LocalZ * EffectiveChunkSize;
					if (H_NegX >= Threshold) SetNeighborBit(1, 0, XBitIdx);
					if (H_PosX >= Threshold) SetNeighborBit(1, 1, XBitIdx);

					// Y neighbors (Index: Z + X * Height)
					const int32 YBitIdx = LocalZ + i * EffectiveChunkHeight;
					if (H_NegY >= Threshold) SetNeighborBit(2, 0, YBitIdx);
					if (H_PosY >= Threshold) SetNeighborBit(2, 1, YBitIdx);

					if (WaterLevel >= Threshold)
					{
						SetNeighborBit(1, 0, XBitIdx, true); SetNeighborBit(1, 1, XBitIdx, true);
						SetNeighborBit(2, 0, YBitIdx, true); SetNeighborBit(2, 1, YBitIdx, true);
					}
				}
			}

			UVoxelObject::GenerateMeshData(Grid, EffectiveVoxelSize,
				[&ColorMap, MapWidth, EffectiveVoxelSize](uint8 BlockType, const FVector& Pos, const FVector& Normal) -> FColor
				{
					if (BlockType == BLOCKTYPE_WATER) return WATER_COLOR;

					// Terrain block -> sample from 2D Biome ColorMap
					const int32 GX = FMath::Clamp(FMath::FloorToInt32(Pos.X / EffectiveVoxelSize), 0, MapWidth - 3);
					const int32 GY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / EffectiveVoxelSize), 0, MapWidth - 3);
					return ColorMap[(GX + 1) * MapWidth + (GY + 1)];
				},
				Result.BlockMeshes,
				&NeighborMasks);
		}

		if (ResultQueue)
		{
			ResultQueue->Enqueue(MoveTemp(Result));
		}
	}
}
