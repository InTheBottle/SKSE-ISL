#include "PCH.h"
#include "Conversion.h"

namespace {

    void InitLogger()
    {
        auto path = SKSE::log::log_directory();
        if (!path)
            return;
        *path /= "SKSE_ISL.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));

#ifndef NDEBUG
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
#else
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
#endif
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
    }

}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    InitLogger();

    if (const auto* decl = SKSE::PluginDeclaration::GetSingleton()) {
        const auto v = decl->GetVersion();
        logger::info("SKSE ISL v{}.{}.{} loading...",
            v.major(), v.minor(), v.patch());
    } else {
        logger::info("SKSE ISL loading...");
    }

    isl::Install();

    return true;
}

SKSEPluginInfo(
    .Version      = { 0, 1, 0, 0 },
    .Name         = "SKSE_ISL",
    .Author       = "Community",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)
