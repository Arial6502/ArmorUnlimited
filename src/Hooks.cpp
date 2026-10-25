#include "Hooks.h"
#include "Detour.h"
#include "Extras.h"
#include "Offsets.h"

namespace Hooks
{
	namespace
	{
		// AddWornItem unequips whatever already occupies the new item's slots behind a `jz`.
		// Turning it into an unconditional `jmp` with the same rel32 skips that while we equip into an extra.
		struct SlotConflictBypass
		{
			static inline std::uint8_t* jump = nullptr;

			SlotConflictBypass()
			{
				jump[0] = 0x48;
				jump[1] = 0xE9;
			}

			~SlotConflictBypass()
			{
				jump[0] = 0x0F;
				jump[1] = 0x84;
			}

			static bool Install()
			{
				jump = reinterpret_cast<std::uint8_t*>(Offsets::AddWornItem.address() + Offsets::AddWornItemSlotConflictJump);
				if (jump[0] != 0x0F || jump[1] != 0x84) {
					logger::error("AddWornItem slot conflict jump not found");
					return false;
				}
				DWORD oldProtect = 0;
				return VirtualProtect(jump, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
			}
		};

		struct BipedAnimEquip
		{
			static void thunk(RE::BipedAnim* biped, float weight, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5)
			{
				std::lock_guard lock(Extras::mutex);
				auto owner = Extras::OwnerOf(biped);
				if (!owner) {
					return original(biped, weight, a3, a4, a5);
				}
				if (!Extras::MarkEquipped(biped)) {
					return;
				}
				biped->root = owner->root;
				original(biped, weight, a3, a4, a5);
				biped->root = owner->root;
			}

			static inline decltype(&thunk) original;
		};

		struct BipedAnimUnequipObject
		{
			static void thunk(RE::BipedAnim* biped, RE::BIPOBJECT* object, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5)
			{
				std::lock_guard lock(Extras::mutex);
				auto owner = Extras::OwnerOf(biped);
				if (owner) {
					biped->root = owner->root;
				}
				original(biped, object, a3, a4, a5);
				if (owner) {
					biped->root = owner->root;
				}
			}

			static inline decltype(&thunk) original;
		};

		struct BipedAnimDtorRemoveAllParts
		{
			static void thunk(RE::BipedAnim* biped, std::uint64_t a2, std::uint64_t a3)
			{
				{
					std::lock_guard lock(Extras::mutex);
					Extras::DestroyAll(biped);
				}
				original(biped, a2, a3);
			}

			static inline REL::Relocation<decltype(thunk)> original;
		};

		struct InitWornArmorAddon
		{
			static void thunk(RE::TESObjectARMA* addon, RE::TESObjectARMO* armor, RE::BSTSmartPointer<RE::BipedAnim>* bipedPtr, std::uint64_t a4)
			{
				std::lock_guard lock(Extras::mutex);
				auto biped = bipedPtr ? bipedPtr->get() : nullptr;
				if (!biped || !biped->root || !Extras::IsHandledArmor(armor) || armor->GetSlotMask() == RE::BGSBipedObjectForm::BipedObjectSlot::kModPelvisSecondary) {
					return original(addon, armor, bipedPtr, a4);
				}
				RE::NiPointer<RE::TESObjectREFR> ref = biped->actorRef.get();
				auto actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor || Extras::IsExcluded(actor) || actor->GetSkin() == armor) {
					return original(addon, armor, bipedPtr, a4);
				}
				auto third = actor->GetBiped1(false).get();
				if (biped != third && biped != actor->GetBiped1(true).get()) {
					return original(addon, armor, bipedPtr, a4);
				}

				// Lookups go through the third person biped on purpose: the player's first person
				// biped only gets its own extra when third person has none for this armor.
				auto extra = Extras::Find(biped, armor->formID);
				if (Extras::Find(third, armor->formID)) {
					if (extra && !extra->root) {
						Extras::Destroy(extra);
						extra = Extras::Create(actor, biped, armor->formID);
					}
				} else if (!extra) {
					extra = Extras::Create(actor, biped, armor->formID);
				}
				if (!extra) {
					return original(addon, armor, bipedPtr, a4);
				}

				RE::BSTSmartPointer<RE::BipedAnim> extraPtr(extra);
				original(addon, armor, &extraPtr, a4);
				Extras::MarkUpdated(extra);
			}

			static inline decltype(&thunk) original;
		};

		struct Update3DModel
		{
			static void FinishExtras(RE::BipedAnim* main, float weight, std::vector<RE::BipedAnim*>* stale)
			{
				for (auto extra : Extras::Of(main)) {
					if (Extras::IsUpdated(extra)) {
						RE::BSTSmartPointer<RE::BipedAnim> hold(extra);
						BipedAnimEquip::thunk(extra, weight, 0, 0, 0);
					} else if (stale) {
						stale->push_back(extra);
					}
				}
			}

			static void HideCoveredSlots(RE::BipedAnim* main, std::uint32_t covered)
			{
				if (!main) {
					return;
				}
				for (std::uint32_t slot = 0; slot < 32; slot++) {
					if (covered & (1u << slot)) {
						BipedAnimUnequipObject::original(main, &main->objects[slot], 1, 0, 0);
						BipedAnimUnequipObject::original(main, &main->bufferedObjects[slot], 1, 0, 0);
					}
				}
			}

			static std::uint32_t CoveredSlots()
			{
				std::uint32_t covered = 0;
				for (auto extra : Extras::Updated()) {
					for (auto& object : extra->objects) {
						if (object.addon) {
							covered |= static_cast<std::uint32_t>(object.addon->GetSlotMask());
						}
						if (auto armor = object.item ? object.item->As<RE::TESObjectARMO>() : nullptr) {
							covered |= static_cast<std::uint32_t>(armor->GetSlotMask());
						}
					}
				}
				return covered;
			}

			static bool thunk(RE::Actor* actor)
			{
				if (!actor || Extras::IsExcluded(actor) || !actor->Is3DLoaded()) {
					return original(actor);
				}
				RE::NiPointer<RE::Actor> hold(actor);
				std::lock_guard lock(Extras::mutex);
				RE::BSTSmartPointer<RE::BipedAnim> third = actor->GetBiped1(false);
				RE::BSTSmartPointer<RE::BipedAnim> first = actor->GetBiped1(true);
				if (first.get() == third.get()) {
					first.reset();
				}
				auto weight = actor->GetWeight() * 0.01f;

				Extras::ClearUpdated();
				auto result = original(actor);
				std::vector<RE::BipedAnim*> stale;
				FinishExtras(third.get(), weight, &stale);
				FinishExtras(first.get(), weight, &stale);
				for (auto extra : stale) {
					Extras::Destroy(extra);
				}

				Extras::ClearUpdated();
				if (actor->Is3DLoaded()) {
					result = original(actor);
				}
				FinishExtras(third.get(), weight, nullptr);
				FinishExtras(first.get(), weight, nullptr);

				auto covered = CoveredSlots();
				HideCoveredSlots(third.get(), covered);
				HideCoveredSlots(first.get(), covered);

				SKSE::NiNodeUpdateEvent event{ actor };
				SKSE::GetNiNodeUpdateEventSource()->SendEvent(&event);
				return result;
			}

			static inline decltype(&thunk) original;
		};

		struct AddWornItem
		{
			static std::uint64_t thunk(RE::Actor* actor, RE::TESBoundObject* item, std::int32_t count, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8)
			{
				if (!actor || !Extras::IsHandledArmor(item) || Extras::IsExcluded(actor)) {
					return original(actor, item, count, a4, a5, a6, a7, a8);
				}
				std::lock_guard lock(Extras::mutex);
				auto main = actor->GetBiped1(false).get();
				if (!main) {
					return original(actor, item, count, a4, a5, a6, a7, a8);
				}
				if (!Extras::Find(main, item->formID)) {
					Extras::Create(actor, main, item->formID);
				}
				SlotConflictBypass bypass;
				return original(actor, item, count, a4, a5, a6, a7, a8);
			}

			static inline decltype(&thunk) original;
		};

		struct UnequipObject
		{
			static std::uint64_t thunk(std::uint64_t a1, RE::Actor* actor, RE::TESBoundObject* item, std::uint64_t a4, std::uint64_t a5)
			{
				std::lock_guard lock(Extras::mutex);
				if (!actor || (item && !Extras::IsHandledArmor(item)) || Extras::IsExcluded(actor) || actor->IsDisabled() ||
					(item && actor->GetSkin() == item->As<RE::TESObjectARMO>())) {
					return original(a1, actor, item, a4, a5);
				}
				auto result = original(a1, actor, item, a4, a5);
				if (actor->Is3DLoaded()) {
					Update3DModel::thunk(actor);
				}
				return result;
			}

			static inline decltype(&thunk) original;
		};
	}

	void Install()
	{
		auto dtorCall = Offsets::BipedAnimDtor.address() + Offsets::BipedAnimDtorRemoveAllPartsCall;
		if (*reinterpret_cast<std::uint8_t*>(dtorCall) == 0xE8) {
			BipedAnimDtorRemoveAllParts::original = SKSE::GetTrampoline().write_call<5>(dtorCall, BipedAnimDtorRemoveAllParts::thunk);
		} else {
			logger::error("BipedAnim dtor RemoveAllParts call not found");
		}

		if (SlotConflictBypass::Install()) {
			Detour<AddWornItem>(Offsets::AddWornItem.address(), "AddWornItem");
		}
		Detour<InitWornArmorAddon>(Offsets::InitWornArmorAddon.address(), "InitWornArmorAddon");
		Detour<UnequipObject>(Offsets::UnequipObject.address(), "UnequipObject");
		Detour<BipedAnimUnequipObject>(Offsets::BipedAnimUnequipObject.address(), "BipedAnim::UnequipObject");
		Detour<Update3DModel>(Offsets::Update3DModel.address(), "Update3DModel");
		Detour<BipedAnimEquip>(Offsets::BipedAnimEquip.address(), "BipedAnim::Equip");
	}
}
