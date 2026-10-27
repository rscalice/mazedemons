#include "AnimGraphNode_MDSupportHandIK.h"

#define LOCTEXT_NAMESPACE "MDSupportHandIK"

FText UAnimGraphNode_MDSupportHandIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "MD Support Hand IK");
}

FText UAnimGraphNode_MDSupportHandIK::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Two-bone IK that puts the left hand on the held gun's Grip_L. Target and alpha come from UMDRangedComponent (no variables to wire). The DisableLHandIK anim curve turns it off.");
}

FText UAnimGraphNode_MDSupportHandIK::GetControllerDescription() const
{
	return LOCTEXT("Description", "MD Support Hand IK");
}

#undef LOCTEXT_NAMESPACE
