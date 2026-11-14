#pragma once

// An actor's main BipedAnim only has one slot per biped object, so each armor that
// would conflict gets its own "extra" BipedAnim sharing the main biped's root node.
namespace Extras
{
	inline std::recursive_mutex mutex;

	bool IsExcluded(RE::Actor* actor);
	bool IsHandledArmor(RE::TESForm* form);

	RE::BipedAnim* Find(RE::BipedAnim* main, RE::FormID armor);
	RE::BipedAnim* OwnerOf(RE::BipedAnim* extra);
	std::vector<RE::BipedAnim*> Of(RE::BipedAnim* main);

	RE::BipedAnim* Create(RE::Actor* actor, RE::BipedAnim* main, RE::FormID armor);
	void Destroy(RE::BipedAnim* extra);
	void DestroyAll(RE::BipedAnim* main);

	bool MarkEquipped(RE::BipedAnim* extra);
	void MarkUpdated(RE::BipedAnim* extra);
	bool IsUpdated(RE::BipedAnim* extra);
	void ClearUpdated();
	const std::set<RE::BipedAnim*>& Updated();
}
