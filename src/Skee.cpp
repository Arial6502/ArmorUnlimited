#include "Skee.h"
#include "Detour.h"
#include "Extras.h"
#include "Offsets.h"

#include <Psapi.h>

// SKEE walks every biped object of an actor through VisitBipeds(actor, std::function), which
// fetches the biped via GetBiped1. We run it once per biped (extras first, main last) and
// redirect GetBiped1 each time. The by-value std::function is destroyed by the callee, so that
// epilogue is NOP'd for every pass but the last.
namespace Skee
{
	namespace
	{
		constexpr std::array<std::uintptr_t, 2> kVisitBipedsCandidates{
			0xc65a0,  // 0.4.20.0
			0xc2950,  // 0.4.19.16
		};
		constexpr std::uintptr_t kGetBiped1Call = 0x28;
		constexpr std::array<std::uint8_t, 6> kGetBiped1CallBytes{ 0xff, 0x90, 0xf0, 0x03, 0x00, 0x00 };
		constexpr std::uintptr_t kRunOnce = 0x179;
		constexpr std::size_t kRunOnceSize = 0x1d;
		constexpr std::array<std::uint8_t, kRunOnceSize> kRunOnceBytes{
			0x49, 0x8b, 0x4d, 0x38, 0x48, 0x85, 0xc9, 0x74, 0x14, 0x48, 0x8b, 0x01, 0x49, 0x3b, 0xcd,
			0x0f, 0x95, 0xc2, 0xff, 0x50, 0x20, 0x49, 0xc7, 0x45, 0x38, 0x00, 0x00, 0x00, 0x00
		};

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

		bool Matches(std::uintptr_t address, std::span<const std::uint8_t> bytes)
		{
			return std::memcmp(reinterpret_cast<void*>(address), bytes.data(), bytes.size()) == 0;
		}

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

		struct VisitBipeds
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
						REL::safe_write(runOnce, kRunOnceBytes.data(), kRunOnceSize);
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
		for (auto offset : kVisitBipedsCandidates) {
			if (offset + kRunOnce + kRunOnceSize > info.SizeOfImage) {
				continue;
			}
			auto visitBipeds = base + offset;
			if (!Matches(visitBipeds + kGetBiped1Call, kGetBiped1CallBytes) || !Matches(visitBipeds + kRunOnce, kRunOnceBytes)) {
				continue;
			}
			runOnce = visitBipeds + kRunOnce;
			if (Detour<VisitBipeds>(visitBipeds, "skee64 VisitBipeds")) {
				logger::info("skee64.dll hooked at +{:x}", offset);
			}
			return;
		}
		logger::error("Unsupported skee64.dll version");
	}
}
