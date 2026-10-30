#pragma once

namespace Offsets
{
	inline constexpr REL::ID BipedAnimCtor{ 15655 };
	inline constexpr REL::ID BipedAnimDtor{ 15656 };
	inline constexpr std::ptrdiff_t BipedAnimDtorRemoveAllPartsCall = 0x12;
	inline constexpr REL::ID BipedAnimRemoveAllParts{ 15659 };
	inline constexpr REL::ID BipedAnimUnequipObject{ 15661 };
	inline constexpr REL::ID BipedAnimEquip{ 15678 };
	inline constexpr REL::ID InitWornArmorAddon{ 17759 };
	inline constexpr REL::ID Update3DModel{ 19743 };
	inline constexpr REL::ID AddWornItem{ 38004 };
	inline constexpr std::ptrdiff_t AddWornItemSlotConflictJump = 0x6b;
	inline constexpr REL::ID UnequipObject{ 38934 };
}
