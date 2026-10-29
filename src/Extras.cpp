#include "Extras.h"
#include "Offsets.h"

namespace Extras
{
	namespace
	{
		constexpr std::uint32_t kPassthroughSlots = 0x8c;

		std::map<RE::BipedAnim*, std::map<RE::FormID, RE::BipedAnim*>> byMain;
		std::map<RE::BipedAnim*, RE::BipedAnim*> owners;
		std::set<RE::BipedAnim*> equipped;
		std::set<RE::BipedAnim*> updated;

		void Construct(RE::BipedAnim* biped, RE::Actor* actor, RE::NiNode* root)
		{
			using func_t = void(RE::BipedAnim*, RE::Actor*, RE::NiNode*);
			REL::Relocation<func_t> func{ Offsets::BipedAnimCtor };
			func(biped, actor, root);
		}

		void Destruct(RE::BipedAnim* biped)
		{
			using func_t = void(RE::BipedAnim*);
			REL::Relocation<func_t> func{ Offsets::BipedAnimDtor };
			func(biped);
		}

		void RemoveAllParts(RE::BipedAnim* biped)
		{
			using func_t = void(RE::BipedAnim*, std::uint64_t, std::uint64_t);
			REL::Relocation<func_t> func{ Offsets::BipedAnimRemoveAllParts };
			func(biped, 1, 0);
		}
	}

	bool IsExcluded(RE::Actor* actor)
	{
		static RE::TESFaction* creatureFaction = nullptr;
		if (!creatureFaction) {
			creatureFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("CreatureFaction");
		}
		return !creatureFaction || actor->IsInFaction(creatureFaction);
	}

	bool IsHandledArmor(RE::TESForm* form)
	{
		auto armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
		return armor && (static_cast<std::uint32_t>(armor->GetSlotMask()) & kPassthroughSlots) == 0;
	}

	RE::BipedAnim* Find(RE::BipedAnim* main, RE::FormID armor)
	{
		auto it = byMain.find(main);
		if (it == byMain.end()) {
			return nullptr;
		}
		auto extra = it->second.find(armor);
		return extra != it->second.end() ? extra->second : nullptr;
	}

	RE::BipedAnim* OwnerOf(RE::BipedAnim* extra)
	{
		auto it = owners.find(extra);
		return it != owners.end() ? it->second : nullptr;
	}

	std::vector<RE::BipedAnim*> Of(RE::BipedAnim* main)
	{
		std::vector<RE::BipedAnim*> result;
		if (auto it = byMain.find(main); it != byMain.end()) {
			for (auto& [armor, extra] : it->second) {
				result.push_back(extra);
			}
		}
		return result;
	}

	RE::BipedAnim* Create(RE::Actor* actor, RE::BipedAnim* main, RE::FormID armor)
	{
		auto extra = static_cast<RE::BipedAnim*>(std::malloc(sizeof(RE::BipedAnim)));
		Construct(extra, actor, main->root);
		extra->root = main->root;
		// Two refs so a game-side smart pointer never drops it to zero and deletes our malloc'd block.
		extra->IncRef();
		extra->IncRef();
		byMain[main][armor] = extra;
		owners[extra] = main;
		return extra;
	}

	void Destroy(RE::BipedAnim* extra)
	{
		if (auto main = OwnerOf(extra)) {
			std::erase_if(byMain[main], [&](auto& entry) { return entry.second == extra; });
		}
		owners.erase(extra);
		equipped.erase(extra);
		updated.erase(extra);
		RemoveAllParts(extra);
		Destruct(extra);
		std::free(extra);
	}

	void DestroyAll(RE::BipedAnim* main)
	{
		for (auto extra : Of(main)) {
			Destroy(extra);
		}
		byMain.erase(main);
	}

	bool MarkEquipped(RE::BipedAnim* extra)
	{
		return equipped.insert(extra).second;
	}

	void MarkUpdated(RE::BipedAnim* extra)
	{
		updated.insert(extra);
	}

	bool IsUpdated(RE::BipedAnim* extra)
	{
		return updated.contains(extra);
	}

	void ClearUpdated()
	{
		updated.clear();
	}
}
