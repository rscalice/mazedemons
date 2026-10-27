// R0: editor wrapper of FAnimNode_MDSupportHandIK so the node can sit in ABP_GenericRetarget's AnimGraph.
#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Ranged/MDAnimNode_SupportHandIK.h"
#include "AnimGraphNode_MDSupportHandIK.generated.h"

UCLASS()
class MAZEDEMONSEDITOR_API UAnimGraphNode_MDSupportHandIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_MDSupportHandIK Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
