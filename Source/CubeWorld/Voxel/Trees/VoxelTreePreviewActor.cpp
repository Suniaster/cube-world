#include "VoxelTreePreviewActor.h"
#include "../VoxelObject.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#endif

AVoxelTreePreviewActor::AVoxelTreePreviewActor()
	: VoxelObject(nullptr)
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SetRootComponent(Root);
}

void AVoxelTreePreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureMaterial();
	Regenerate();
}

void AVoxelTreePreviewActor::EnsureMaterial()
{
	if (Material) return;

#if WITH_EDITOR
	UMaterial* NewMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_VoxelTree_Preview"));

	UMaterialExpressionVertexColor* VertColorExpr = NewObject<UMaterialExpressionVertexColor>(NewMat);
	NewMat->GetExpressionCollection().AddExpression(VertColorExpr);

	if (UMaterialEditorOnlyData* EditorData = NewMat->GetEditorOnlyData())
	{
		EditorData->BaseColor.Expression = VertColorExpr;
	}

	NewMat->PreEditChange(nullptr);
	NewMat->PostEditChange();

	Material = NewMat;
#endif
}

void AVoxelTreePreviewActor::Regenerate()
{
	if (VoxelObject)
	{
		UProceduralMeshComponent* OldMesh = VoxelObject->GetMeshComponent();
		if (OldMesh)
		{
			OldMesh->DestroyComponent();
		}
		VoxelObject = nullptr;
	}

	FVoxelTreeData TreeData = FTreeGenerator::GenerateTree(Params, Seed);

	const FColor WoodColor(101, 67, 33, 255);
	const FColor LeavesColor(34, 139, 34, 255);
	UVoxelObject::GenerateMeshData(TreeData.Grid, VoxelSize,
		[&](uint8 BlockType, const FVector& Pos, const FVector& Normal) -> FColor
		{
			return (BlockType == TREE_BLOCKTYPE_LEAVES) ? LeavesColor : WoodColor;
		},
		TreeData.BlockMeshes);

	VoxelObject = NewObject<UVoxelObject>(this);
	VoxelObject->GetBlockMeshes() = MoveTemp(TreeData.BlockMeshes);
	VoxelObject->Spawn(this, Material, nullptr, false);
}
