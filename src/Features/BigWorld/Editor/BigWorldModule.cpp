#include "BigWorldModule.h"

#include "../../../Application/FeatureRouteRegistry.h"
#include "../../../Core/Logging.h"
#include "../../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../../Editor/EditorFeatureHooks.h"

#include <charconv>
#include <cmath>

namespace gte {

namespace {

constexpr const char* kLogCategory = "BigWorld";
constexpr std::string_view kActionPrefix = "bigworld.";

std::string JsonEscape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

ModuleCommandResult Json(int status, std::string body)
{
    ModuleCommandResult r;
    r.httpStatus = status;
    r.body = std::move(body);
    return r;
}

ModuleCommandResult Fail(int status, const std::string& message)
{
    return Json(status, "{\"success\":false,\"error\":\"" + JsonEscape(message) + "\"}");
}

std::optional<double> ParseNumber(const std::string& text)
{
    double value = 0.0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> ParseFlag(const std::string& text)
{
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return std::nullopt;
}

} // namespace

BigWorldModule::~BigWorldModule() = default;

void BigWorldModule::OnAttach(const EditorModuleAttach& attach)
{
    m_terrain.emplace(attach.modules);
    m_water.emplace(attach.modules);
    m_features.emplace(attach.modules, m_core);
    m_demo = std::make_unique<bigworld::BigWorldDemo>(bigworld::BigWorldPorts{ &*m_terrain, &*m_water, &*m_features });
    if (attach.hooks != nullptr) {
        attach.hooks->AddMenuItem({ "File", "Open Big World", {}, [this] { return OpenFromMenu(); } });
        attach.hooks->AddMenuItem({ "File", "Close Big World", [this] { return m_demo != nullptr && m_demo->Active(); },
            [this] { return CloseFromMenu(); } });
    }
}

EditorActionResult BigWorldModule::OpenFromMenu()
{
    if (m_demo == nullptr) {
        return { false, "the Big World is not available" };
    }
    const bigworld::PortResult opened = m_demo->Open(bigworld::MakeBigWorldPlan(bigworld::Quality::Big), m_now);
    return { opened.ok, "Big World: " + opened.message };
}

EditorActionResult BigWorldModule::CloseFromMenu()
{
    if (m_demo == nullptr) {
        return { false, "the Big World is not available" };
    }
    const bigworld::PortResult closed = m_demo->Close();
    return { closed.ok, "Big World: " + closed.message };
}

void BigWorldModule::LogPhaseChange()
{
    const bigworld::BigWorldState& state = m_demo->State();
    if (state.phase == m_loggedPhase) {
        return;
    }
    m_loggedPhase = state.phase;
    const std::string text = std::string("phase ") + bigworld::PhaseName(state.phase) + ": " + state.status;
    if (state.phase == bigworld::Phase::Failed) {
        GTE_LOG_ERROR(kLogCategory, text);
    } else {
        GTE_LOG_INFO(kLogCategory, text);
    }
    if (!state.info.empty()) {
        GTE_LOG_INFO(kLogCategory, state.info);
    }
    for (const std::string& warning : state.warnings) {
        if (warning == bigworld::kSceneReplacedNote) {
            GTE_LOG_INFO(kLogCategory, warning);
        } else {
            GTE_LOG_WARNING(kLogCategory, warning);
        }
    }
}

void BigWorldModule::OnFrameTick(const EditorFrameTick& tick)
{
    m_now = tick.nowSeconds;
    if (m_demo == nullptr) {
        return;
    }
    m_demo->Tick(tick.nowSeconds);
    LogPhaseChange();
}

ModuleCommandResult BigWorldModule::StateCommand()
{
    m_demo->Refresh(m_now);
    return Json(200, bigworld::BigWorldStateToJson(m_demo->State()));
}

ModuleCommandResult BigWorldModule::OpenCommand(const ModuleCommandRequest& request)
{
    bigworld::Quality quality = bigworld::Quality::Big;
    for (const auto& [key, value] : request.params) {
        if (key == "quality" && !bigworld::TryParseQuality(value, quality)) {
            return Fail(400, "quality must be lean, medium or big");
        }
    }
    bigworld::BigWorldPlan plan = bigworld::MakeBigWorldPlan(quality);
    for (const auto& [key, value] : request.params) {
        if (key == "quality") {
            continue;
        }
        if (key == "lake" || key == "river" || key == "free" || key == "oceanQuad") {
            const std::optional<bool> flag = ParseFlag(value);
            if (!flag) {
                return Fail(400, "bad bool for " + key);
            }
            (key == "lake" ? plan.lake : key == "river" ? plan.river : key == "free" ? plan.freeCamera : plan.flatOceanQuad) = *flag;
            continue;
        }
        const std::optional<double> number = ParseNumber(value);
        if (!number) {
            return Fail(400, "bad number for " + key);
        }
        if (key == "x") {
            plan.spotX = *number;
        } else if (key == "z") {
            plan.spotZ = *number;
        } else if (key == "alt") {
            plan.heightAboveGround = *number;
        } else if (key == "heading") {
            plan.headingDegrees = *number;
        } else if (key == "lookDown") {
            plan.lookDownDegrees = *number;
        } else if (key == "lakeDistance") {
            plan.lakeDistance = *number;
        } else if (key == "lakeSize") {
            plan.lakeSize = *number;
        } else if (key == "coverage") {
            bigworld::SetCloudCoverage(plan, *number);
        } else {
            return Fail(400, "unknown open option: " + key);
        }
    }
    const bigworld::PortResult opened = m_demo->Open(plan, m_now);
    if (!opened.ok) {
        return Fail(409, opened.message);
    }
    return Json(200, bigworld::BigWorldStateToJson(m_demo->State()));
}

std::optional<ModuleCommandResult> BigWorldModule::OnCommand(const ModuleCommandRequest& request)
{
    if (m_demo == nullptr || request.action.rfind(kActionPrefix, 0) != 0) {
        return std::nullopt;
    }
    const std::string action = request.action.substr(kActionPrefix.size());
    if (action == "state") {
        return StateCommand();
    }
    if (action == "open") {
        return OpenCommand(request);
    }
    if (action == "close") {
        const bigworld::PortResult closed = m_demo->Close();
        return Json(closed.ok ? 200 : 409,
            std::string("{\"success\":") + (closed.ok ? "true" : "false") + ",\"message\":\"" + JsonEscape(closed.message) + "\"}");
    }
    return Fail(404, "unknown bigworld action: " + action);
}

void BigWorldModule::OnShutdown()
{
    if (m_demo != nullptr) {
        m_demo->Close();
    }
    m_demo.reset();
    m_features.reset();
    m_water.reset();
    m_terrain.reset();
}

namespace {

std::unique_ptr<IEngineFeatureModule> MakeBigWorldModule(Core& core)
{
    return std::make_unique<BigWorldModule>(core);
}

} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("BigWorld", &MakeBigWorldModule);
GTE_REGISTER_MODULE_ROUTES("bigworld", "bigworld.", "state");

} // namespace gte
