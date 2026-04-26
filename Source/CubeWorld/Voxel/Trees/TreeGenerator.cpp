#include "TreeGenerator.h"
#include "Math/RandomStream.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BoxElem.h"

struct FScaNode
{
	FVector Position;
	int32 ParentIndex;
	FVector GrowthDirection;
	int32 AttractionCount;
	bool bIsTerminal;

	FScaNode(FVector InPos, int32 InParent)
		: Position(InPos), ParentIndex(InParent), GrowthDirection(FVector::ZeroVector), AttractionCount(0), bIsTerminal(true)
	{}
};

FVoxelTreeData FTreeGenerator::GenerateTree(const FVoxelTreeParams& Params, int32 Seed)
{
	FRandomStream Random(Seed);

	TArray<FVector> AttractionPoints;
	for (int32 i = 0; i < Params.AttractionPoints; ++i)
	{
		FVector Point = Random.VRand() * Random.FRandRange(0.0f, Params.CrownRadius);
		Point.Z += Params.TrunkHeight + (Params.CrownRadius * 0.5f);
		AttractionPoints.Add(Point);
	}

	TArray<FScaNode> Nodes;
	Nodes.Add(FScaNode(FVector(0, 0, 0), -1)); // root

	int32 LastNodeIdx = 0;
	int32 TrunkSteps = FMath::CeilToInt32(Params.TrunkHeight / Params.BranchLength);
	for (int32 i = 0; i < TrunkSteps; ++i)
	{
		FVector NextPos = Nodes[LastNodeIdx].Position + FVector(0, 0, Params.BranchLength);
		Nodes[LastNodeIdx].bIsTerminal = false;
		Nodes.Add(FScaNode(NextPos, LastNodeIdx));
		LastNodeIdx = Nodes.Num() - 1;
	}

	bool bGrew = true;
	int32 Step = 0;
	const float KillDistSq = Params.KillDistance * Params.KillDistance;
	const float AttractDistSq = Params.AttractionDistance * Params.AttractionDistance;

	int32 FirstNewNodeIdx = 0;
	while (bGrew && AttractionPoints.Num() > 0 && Step < Params.MaxSteps)
	{
		bGrew = false;
		Step++;

		// Remove reached points using only newly added nodes from the previous step
		for (int32 i = AttractionPoints.Num() - 1; i >= 0; --i)
		{
			bool bKilled = false;
			for (int32 n = FirstNewNodeIdx; n < Nodes.Num(); ++n)
			{
				if (FVector::DistSquared(AttractionPoints[i], Nodes[n].Position) < KillDistSq)
				{
					bKilled = true;
					break;
				}
			}
			if (bKilled)
			{
				AttractionPoints.RemoveAtSwap(i);
			}
		}
		FirstNewNodeIdx = Nodes.Num();

		// Calc attraction
		for (const FVector& Point : AttractionPoints)
		{
			int32 ClosestNodeIdx = -1;
			float MinDistSq = AttractDistSq;

			for (int32 n = 0; n < Nodes.Num(); ++n)
			{
				float DistSq = FVector::DistSquared(Point, Nodes[n].Position);
				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					ClosestNodeIdx = n;
				}
			}

			if (ClosestNodeIdx != -1)
			{
				FVector Dir = (Point - Nodes[ClosestNodeIdx].Position).GetSafeNormal();
				Nodes[ClosestNodeIdx].GrowthDirection += Dir;
				Nodes[ClosestNodeIdx].AttractionCount++;
			}
		}

		// Grow branches
		int32 InitialNodeCount = Nodes.Num();
		for (int32 n = 0; n < InitialNodeCount; ++n)
		{
			if (Nodes[n].AttractionCount > 0)
			{
				FVector GrowDir = (Nodes[n].GrowthDirection / static_cast<float>(Nodes[n].AttractionCount)).GetSafeNormal();
				GrowDir += Random.VRand() * 0.15f;
				GrowDir.Normalize();

				FVector NewPos = Nodes[n].Position + (GrowDir * Params.BranchLength);
				Nodes[n].bIsTerminal = false;
				Nodes.Add(FScaNode(NewPos, n));
				
				Nodes[n].GrowthDirection = FVector::ZeroVector;
				Nodes[n].AttractionCount = 0;
				bGrew = true;
			}
		}
	}

	// Rasterize into VoxelGrid
	FVector MinBounds(0, 0, 0);
	FVector MaxBounds(0, 0, 0);
	for (const FScaNode& Node : Nodes)
	{
		MinBounds.X = FMath::Min(MinBounds.X, Node.Position.X);
		MinBounds.Y = FMath::Min(MinBounds.Y, Node.Position.Y);
		MinBounds.Z = FMath::Min(MinBounds.Z, Node.Position.Z);
		MaxBounds.X = FMath::Max(MaxBounds.X, Node.Position.X);
		MaxBounds.Y = FMath::Max(MaxBounds.Y, Node.Position.Y);
		MaxBounds.Z = FMath::Max(MaxBounds.Z, Node.Position.Z);
	}

	const float Padding = FMath::Max(Params.FoliageRadius, Params.TrunkRadius) + 2.0f;
	MinBounds = FVector(FMath::FloorToFloat(MinBounds.X - Padding), FMath::FloorToFloat(MinBounds.Y - Padding), FMath::FloorToFloat(MinBounds.Z - Padding));
	MaxBounds = FVector(FMath::CeilToFloat(MaxBounds.X + Padding), FMath::CeilToFloat(MaxBounds.Y + Padding), FMath::CeilToFloat(MaxBounds.Z + Padding));

	const int32 GridSizeX = FMath::RoundToInt32(MaxBounds.X - MinBounds.X);
	const int32 GridSizeY = FMath::RoundToInt32(MaxBounds.Y - MinBounds.Y);
	const int32 GridSizeZ = FMath::RoundToInt32(MaxBounds.Z - MinBounds.Z);

	FVoxelTreeData Result;
	Result.Grid = FVoxelGrid3D(GridSizeX, GridSizeY, GridSizeZ);
	Result.CenterOffset = FIntVector(
		FMath::FloorToInt32(-MinBounds.X),
		FMath::FloorToInt32(-MinBounds.Y),
		FMath::FloorToInt32(-MinBounds.Z)
	);
	
	// Define trunk collision box in Grid space (voxel units)
	// Trunk is centered on the voxel grid, so we add 0.5 offset to X and Y.
	Result.TrunkCollision.Center = FVector(Result.CenterOffset) + FVector(0.5f, 0.5f, Params.TrunkHeight * 0.5f);
	Result.TrunkCollision.Size = FVector(Params.TrunkRadius * 2.f, Params.TrunkRadius * 2.f, Params.TrunkHeight);

	auto WorldToGrid = [&](const FVector& Pos) -> FIntVector
	{
		return FIntVector(
			FMath::FloorToInt32(Pos.X - MinBounds.X),
			FMath::FloorToInt32(Pos.Y - MinBounds.Y),
			FMath::FloorToInt32(Pos.Z - MinBounds.Z)
		);
	};

	// Trunk nodes are the first TrunkSteps+1 nodes (root + straight vertical segments).
	const int32 TrunkNodeEnd = TrunkSteps;
	const int32 TrunkRadInt = FMath::CeilToInt32(Params.TrunkRadius);
	const float TrunkRadSq = Params.TrunkRadius * Params.TrunkRadius;

	auto StampDisc = [&](const FIntVector& GridP, int32 RadInt, float RadSq)
	{
		for (int32 dx = -RadInt; dx <= RadInt; ++dx)
		{
			for (int32 dy = -RadInt; dy <= RadInt; ++dy)
			{
				if (static_cast<float>(dx*dx + dy*dy) > RadSq) continue;
				Result.Grid.SetVoxel(GridP.X + dx, GridP.Y + dy, GridP.Z, TREE_BLOCKTYPE_WOOD);
			}
		}
	};

	// Draw wood lines
	for (int32 n = 1; n < Nodes.Num(); ++n)
	{
		const bool bIsTrunkSegment = (n <= TrunkNodeEnd) && (Nodes[n].ParentIndex < TrunkNodeEnd);

		FVector Start = Nodes[Nodes[n].ParentIndex].Position;
		FVector End = Nodes[n].Position;

		float Dist = FVector::Distance(Start, End);
		int32 Samples = FMath::Max(2, FMath::CeilToInt32(Dist * 2.0f));

		for (int32 s = 0; s <= Samples; ++s)
		{
			float Alpha = static_cast<float>(s) / static_cast<float>(Samples);
			FVector P = FMath::Lerp(Start, End, Alpha);
			FIntVector GridP = WorldToGrid(P);

			if (bIsTrunkSegment && TrunkRadInt > 0)
			{
				StampDisc(GridP, TrunkRadInt, TrunkRadSq);
			}
			else
			{
				Result.Grid.SetVoxel(GridP.X, GridP.Y, GridP.Z, TREE_BLOCKTYPE_WOOD);
			}
		}
	}

	// Draw leaves
	const int32 LeafRadSq = FMath::CeilToInt32(Params.FoliageRadius * Params.FoliageRadius);
	const int32 LeafRadInt = FMath::CeilToInt32(Params.FoliageRadius);

	for (const FScaNode& Node : Nodes)
	{
		if (!Node.bIsTerminal) continue;

		FIntVector Center = WorldToGrid(Node.Position);

		for (int32 dx = -LeafRadInt; dx <= LeafRadInt; ++dx)
		{
			for (int32 dy = -LeafRadInt; dy <= LeafRadInt; ++dy)
			{
				for (int32 dz = -LeafRadInt; dz <= LeafRadInt; ++dz)
				{
					const int32 DistSq = dx*dx + dy*dy + dz*dz;
					if (DistSq > LeafRadSq) continue;

					const int32 X = Center.X + dx;
					const int32 Y = Center.Y + dy;
					const int32 Z = Center.Z + dz;

					if (!Result.Grid.IsValidIndex(X, Y, Z) || Result.Grid.GetVoxel(X, Y, Z) != 0) continue;

					if (DistSq > LeafRadSq * 0.7f && Random.FRand() > 0.5f) continue;

					Result.Grid.SetVoxel(X, Y, Z, TREE_BLOCKTYPE_LEAVES);
				}
			}
		}
	}
	return Result;
}

void FTreeGenerator::AddTrunkCollision(UStaticMesh* Mesh, const FVoxelTreeCollision& Collision, float VoxelSize)
{
	if (!Mesh) return;

	if (UBodySetup* BodySetup = Mesh->GetBodySetup())
	{
		FKAggregateGeom& AggGeom = BodySetup->AggGeom;
		FKBoxElem BoxElem;
		BoxElem.Center = Collision.Center * VoxelSize;
		BoxElem.X = Collision.Size.X * VoxelSize;
		BoxElem.Y = Collision.Size.Y * VoxelSize;
		BoxElem.Z = Collision.Size.Z * VoxelSize;
		AggGeom.BoxElems.Add(BoxElem);

		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
	}
}
