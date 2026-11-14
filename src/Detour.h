#pragma once

#include <detours/detours.h>

template <class T>
bool Detour(std::uintptr_t target, std::string_view name)
{
	T::original = reinterpret_cast<decltype(T::original)>(target);
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(reinterpret_cast<PVOID*>(&T::original), reinterpret_cast<PVOID>(&T::thunk));
	if (DetourTransactionCommit() != NO_ERROR) {
		logger::error("Failed to hook {}", name);
		return false;
	}
	return true;
}
