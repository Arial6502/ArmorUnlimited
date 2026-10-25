#include "Skee.h"
#include "Detour.h"
#include "Extras.h"
#include "Offsets.h"

#include <Psapi.h>

// SKEE applies body morphs through a function that fetches the actor's biped via GetBiped1.
// We run that function once per biped (extras first, main last) and redirect GetBiped1 each time.
namespace Skee
{
	namespace
	{
		// skee64.dll 0.4.19.16
		constexpr std::uintptr_t kApplyMorphs = 0xc2950;
		constexpr std::uintptr_t kGetBiped1Call = 0x28;
		constexpr std::uintptr_t kRunOnce = 0xc2ac9;
		constexpr std::size_t kRunOnceSize = 0x1d;
		constexpr std::array<std::uint8_t, 6> kGetBiped1CallBytes{ 0xff, 0x90, 0xf0, 0x03, 0x00, 0x00 };

		using BipedList = std::vector<RE::BSTSmartPointer<RE::BipedAnim>>;

		struct Redirect
		{
			RE::TESObjectREFR* actor = nullptr;
			BipedList third;
			BipedList first;
			std::size_t index = 0;
		};

		thread_local Redirect redirect;
		std::uintptr_t runOnce = 0;
		std::array<std::uint8_t, kRunOnceSize> runOnceBytes{};

		const RE::BSTSmartPointer<RE::BipedAnim>* Redirected(RE::TESObjectREFR* ref, bool firstPerson)
		{
			if (!redirect.actor || ref != redirect.actor) {
				return nullptr;
			}
			static const RE::BSTSmartPointer<RE::BipedAnim> none;
			auto& list = firstPerson ? redirect.first : redirect.third;
			return redirect.index < list.size() ? &list[redirect.index] : &none;
		}

		BipedList Collect(RE::BipedAnim* main)
		{
			BipedList list;
			for (auto extra : Extras::Of(main)) {
				list.emplace_back(extra);
			}
			list.emplace_back(main);
			return list;
		}

		struct PlayerGetBiped1
		{
			static const RE::BSTSmartPointer<RE::BipedAnim>& thunk(RE::PlayerCharacter* player, bool firstPerson)
			{
				if (auto biped = Redirected(player, firstPerson)) {
					return *biped;
				}
				return original(player, firstPerson);
			}

			static inline decltype(&thunk) original;
		};

		// NPCs use the shared TESObjectREFR::GetBiped1 through the Character vtable.
		struct CharacterGetBiped1
		{
			static const RE::BSTSmartPointer<RE::BipedAnim>& thunk(RE::Character* character, bool firstPerson)
			{
				if (auto biped = Redirected(character, firstPerson)) {
					return *biped;
				}
				return original(character, firstPerson);
			}

			static inline REL::Relocation<decltype(thunk)> original;
		};

		struct ApplyMorphs
		{
			static void thunk(RE::Actor* actor, void* callback)
			{
				if (!actor) {
					return;
				}
				if (Extras::IsExcluded(actor)) {
					return original(actor, callback);
				}
				std::lock_guard lock(Extras::mutex);
				redirect.third = Collect(actor->GetBiped1(false).get());
				redirect.first = Collect(actor->GetBiped1(true).get());
				redirect.actor = actor;

				REL::safe_fill(runOnce, 0x90, kRunOnceSize);
				for (auto i = std::max(redirect.third.size(), redirect.first.size()); i-- > 0;) {
					redirect.index = i;
					if (i == 0) {
						REL::safe_write(runOnce, runOnceBytes.data(), kRunOnceSize);
					}
					original(actor, callback);
				}
				redirect = {};
			}

			static inline decltype(&thunk) original;
		};
	}

	void Install()
	{
		Detour<PlayerGetBiped1>(Offsets::PlayerCharacterGetBiped1.address(), "PlayerCharacter::GetBiped1");
		REL::Relocation<std::uintptr_t> characterVtbl{ RE::VTABLE_Character[0] };
		CharacterGetBiped1::original = characterVtbl.write_vfunc(0x7E, CharacterGetBiped1::thunk);

		auto handle = GetModuleHandleA("skee64.dll");
		if (!handle) {
			logger::error("skee64.dll not loaded ({})", GetLastError());
			return;
		}
		MODULEINFO info{};
		GetModuleInformation(GetCurrentProcess(), handle, &info, sizeof(info));
		auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
		if (std::memcmp(reinterpret_cast<void*>(base + kApplyMorphs + kGetBiped1Call), kGetBiped1CallBytes.data(), kGetBiped1CallBytes.size()) != 0) {
			logger::error("Unsupported skee64.dll version");
			return;
		}
		runOnce = base + kRunOnce;
		std::memcpy(runOnceBytes.data(), reinterpret_cast<void*>(runOnce), kRunOnceSize);
		if (Detour<ApplyMorphs>(base + kApplyMorphs, "skee64 ApplyMorphs")) {
			logger::info("skee64.dll hooked");
		}
	}
}
