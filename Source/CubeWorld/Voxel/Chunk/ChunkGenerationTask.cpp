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
	float ChunkCenterX = ChunkWorldX + (ChunkSize * VoxelSize * 0.5f);
	float ChunkCenterY = ChunkWorldY + (ChunkSize * VoxelSize * 0.5f);
	FVoxelTerrainNoise::FCachedWorleyPoints CachedPoints = FVoxelTerrainNoise::GetCachedWorleyPoints(
		ChunkCenterX, ChunkCenterY, BiomeCellSize, Seed, BiomeCount);

	// ── Macro-Grid Bilinear Interpolation ──
	const int32 InterpolationStep = 8;
	const int32 StepX = FMath::Min(InterpolationStep, EffectiveChunkSize);
	const int32 StepY = FMath::Min(InterpolationStep, EffectiveChunkSize);
	
	const int32 MacroNodesX = (EffectiveChunkSize / StepX) + 1;
	const int32 MacroNodesY = (EffectiveChunkSize / StepY) + 1;
	const int32 TotalMacroNodes = MacroNodesX * MacroNodesY;

	TArray<float> MacroHeight;
	TArray<uint8> MacroBiome;
	TArray<FLinearColor> MacroColor;
	
	MacroHeight.SetNumUninitialized(TotalMacroNodes);
	MacroBiome.SetNumUninitialized(TotalMacroNodes);
	MacroColor.SetNumUninitialized(TotalMacroNodes);

	// 1. Evaluate Procedural Noise exactly on Macro Nodes
	for (int32 MX = 0; MX < MacroNodesX; ++MX)
	{
		for (int32 MY = 0; MY < MacroNodesY; ++MY)
		{
			// Safe evaluation exactly on chunk borders to prevent seams
			const float SampleX = ChunkWorldX + (MX * StepX + 0.5f) * EffectiveVoxelSize;
			const float SampleY = ChunkWorldY + (MY * StepY + 0.5f) * EffectiveVoxelSize;

			uint8 PrimaryBiome = 0;
			FColor VoxelColor;
			int32 H = FVoxelTerrainNoise::GetWeightedHeightForLocation(
				SampleX, SampleY, BiomeCellSize, Seed, Biomes, BlendWidth, CachedPoints, PrimaryBiome, VoxelColor);

			int32 MIdx = MX * MacroNodesY + MY;
			MacroHeight[MIdx] = static_cast<float>(H);
			MacroBiome[MIdx] = PrimaryBiome;
			MacroColor[MIdx] = VoxelColor.ReinterpretAsLinear();
		}
	}

	// ── 1D Mapping ──
	const int32 TotalColumns = EffectiveChunkSize * EffectiveChunkSize;
	
	TArray<int32> HeightMap;
	HeightMap.SetNumUninitialized(TotalColumns);
	
	TArray<uint8> BlockTypeMap;
	BlockTypeMap.SetNumUninitialized(TotalColumns);
	
	TArray<FColor> ColorMap;
	ColorMap.SetNumUninitialized(TotalColumns);

	int32 AbsoluteMaxHeight = 0;

	// 2. Bilinearly Interpolate the 1D Buffers
	int32 ColIndex = 0;
	for (int32 X = 0; X < EffectiveChunkSize; ++X)
	{
		int32 MX0 = X / StepX;
		int32 MX1 = FMath::Min(MX0 + 1, MacroNodesX - 1);
		float TX  = static_cast<float>(X % StepX) / static_cast<float>(StepX);
		
		for (int32 Y = 0; Y < EffectiveChunkSize; ++Y, ++ColIndex)
		{
			int32 MY0 = Y / StepY;
			int32 MY1 = FMath::Min(MY0 + 1, MacroNodesY - 1);
			float TY  = static_cast<float>(Y % StepY) / static_cast<float>(StepY);

			// Interpolate Height
			float H00 = MacroHeight[MX0 * MacroNodesY + MY0];
			float H10 = MacroHeight[MX1 * MacroNodesY + MY0];
			float H01 = MacroHeight[MX0 * MacroNodesY + MY1];
			float H11 = MacroHeight[MX1 * MacroNodesY + MY1];

			float H0 = H00 + (H10 - H00) * TX;
			float H1 = H01 + (H11 - H01) * TX;
			int32 FinalH = FMath::RoundToInt32(H0 + (H1 - H0) * TY);

			// Nearest-Neighbor for Biome selection (to avoid block type blending)
			int32 NearestMX = (TX < 0.5f) ? MX0 : MX1;
			int32 NearestMY = (TY < 0.5f) ? MY0 : MY1;
			uint8 FinalBiome = MacroBiome[NearestMX * MacroNodesY + NearestMY];

			// Interpolate Color
			FLinearColor C00 = MacroColor[MX0 * MacroNodesY + MY0];
			FLinearColor C10 = MacroColor[MX1 * MacroNodesY + MY0];
			FLinearColor C01 = MacroColor[MX0 * MacroNodesY + MY1];
			FLinearColor C11 = MacroColor[MX1 * MacroNodesY + MY1];

			FLinearColor C0 = C00 + (C10 - C00) * TX;
			FLinearColor C1 = C01 + (C11 - C01) * TX;
			FColor FinalColor = (C0 + (C1 - C0) * TY).ToFColor(true);

			HeightMap[ColIndex] = FinalH;
			BlockTypeMap[ColIndex] = FinalBiome;
			ColorMap[ColIndex] = FinalColor;

			if (FinalH > AbsoluteMaxHeight) AbsoluteMaxHeight = FinalH;
		}
	}

	// ── ZLayer Generation Dispatch ──
	// Cap absolute height generation structurally 
	int32 ActiveZLayers = FMath::CeilToInt32(static_cast<float>(AbsoluteMaxHeight) / static_cast<float>(ChunkHeight));
	ActiveZLayers = FMath::Clamp(ActiveZLayers, 1, 100);

	for (int32 ZLayerIdx = 0; ZLayerIdx < ActiveZLayers; ++ZLayerIdx)
	{
		const int32 ZBase = ZLayerIdx * ChunkHeight;
		bool bHasAnyBlocks = false;
		
		// Quick verification pass to see if this specific layer has blocks
		for (int32 i = 0; i < TotalColumns; ++i)
		{
			if (HeightMap[i] > ZBase)
			{
				bHasAnyBlocks = true;
				break;
			}
		}

		FChunkGenerationResult Result;
		Result.ChunkCoord = ChunkCoord;
		Result.ZLayer = ZLayerIdx;
		Result.LODLevel = LODLevel;
		Result.bSuccess = true;
		Result.bHasAnyBlocks = bHasAnyBlocks;

		if (bHasAnyBlocks)
		{
			// Voxel grid — each effective voxel spans LODScale full-res voxels
			FVoxelGrid3D Grid(EffectiveChunkSize, EffectiveChunkSize, EffectiveChunkHeight);
			ColIndex = 0;
			for (int32 X = 0; X < EffectiveChunkSize; ++X)
			{
				for (int32 Y = 0; Y < EffectiveChunkSize; ++Y, ++ColIndex)
				{
					const uint8 BlockType = BlockTypeMap[ColIndex];
					const int32 ColHeight = HeightMap[ColIndex];

					// Branchless Z limit: Block calculation dynamically avoids logical `if` queries
					int32 LimitZ = FMath::Clamp((ColHeight - ZBase + LODScale - 1) / LODScale, 0, EffectiveChunkHeight);

					for (int32 LocalZ = 0; LocalZ < LimitZ; ++LocalZ)
					{
						Grid.SetVoxel(X, Y, LocalZ, BlockType);
					}
				}
			}

			// Mesh — EffectiveVoxelSize covers XY and Z correctly
			UVoxelObject::GenerateMeshData(Grid, EffectiveVoxelSize,
				[&ColorMap, EffectiveChunkSize, EffectiveVoxelSize](uint8 /*BlockType*/, const FVector& Pos, const FVector& /*Normal*/) -> FColor
				{
					int32 GX = FMath::Clamp(FMath::FloorToInt32(Pos.X / EffectiveVoxelSize), 0, EffectiveChunkSize - 1);
					int32 GY = FMath::Clamp(FMath::FloorToInt32(Pos.Y / EffectiveVoxelSize), 0, EffectiveChunkSize - 1);

					return ColorMap[GX * EffectiveChunkSize + GY];
				},
				Result.MeshData);
		}

		if (ResultQueue)
		{
			ResultQueue->Enqueue(MoveTemp(Result));
		}
	}
}
