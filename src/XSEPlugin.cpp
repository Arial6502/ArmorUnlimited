#include "Extras.h"
#include "Hooks.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= std::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		if (message->type == SKSE::MessagingInterface::kPostPostLoad) {
			Hooks::Install();
		}
	}
}

using ExtraPartVisitor = void (*)(void* context, std::uint32_t slot, RE::TESForm* item, RE::TESObjectARMA* addon, RE::NiAVObject* object);

// Lets other plugins reach armor parts that live on extra bipeds. Parts are copied out first so
// the visitor runs with no ArmorUnlimited lock held and each part stays alive for the whole visit.
extern "C" __declspec(dllexport) void ArmorUnlimited_VisitExtraParts(RE::BipedAnim* main, ExtraPartVisitor visitor, void* context)
{
	for (auto& part : Extras::PartsOf(main)) {
		visitor(context, part.slot, part.item, part.addon, part.object.get());
	}
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("Loaded plugin {} {}", Plugin::NAME, Plugin::VERSION.string());

	if (!REL::Module::IsAE()) {
		logger::error("Only Skyrim AE 1.6.1170 is supported");
		return false;
	}

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(64);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}

extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}
