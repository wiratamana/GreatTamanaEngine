#include "WaterCommands.h"

#include "../../Encoding/PngEncoder.h"
#include "../../Features/Water/World/WaterEdit.h"
#include "../../Features/Water/World/WaterJson.h"
#include "../../Features/Water/World/WaterMapImage.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <map>
#include <optional>
#include <sstream>

namespace gte {

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

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

TerrainCommandResult Fail(int status, const std::string& message)
{
    TerrainCommandResult r;
    r.httpStatus = status;
    r.body = "{\"success\":false,\"error\":\"" + JsonEscape(message) + "\"}";
    return r;
}

TerrainCommandResult Json(const std::string& body)
{
    TerrainCommandResult r;
    r.body = body;
    return r;
}

TerrainCommandResult Done(const water::WaterResult& outcome)
{
    if (!outcome.ok) {
        return Fail(409, outcome.message);
    }
    return Json("{\"success\":true,\"message\":\"" + JsonEscape(outcome.message) + "\",\"id\":" + std::to_string(outcome.id) + "}");
}

bool ParseNumber(const std::string& text, double& out)
{
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(out);
}

bool ParseFlag(const std::string& text, bool& out)
{
    if (text == "true" || text == "1") { out = true; return true; }
    if (text == "false" || text == "0") { out = false; return true; }
    return false;
}

// Query values by name with typed, checked reads. The first error stays in `error`.
class Query {
public:
    explicit Query(const Pairs& pairs)
    {
        for (const auto& [k, v] : pairs) {
            m_values[k] = v;
        }
    }

    bool Has(const std::string& key) const { return m_values.contains(key); }
    const std::string* Text(const std::string& key) const
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? nullptr : &it->second;
    }
    std::optional<double> Number(const std::string& key)
    {
        const std::string* t = Text(key);
        if (t == nullptr) {
            return std::nullopt;
        }
        double v = 0.0;
        if (!ParseNumber(*t, v)) {
            Bad("bad number for " + key + ": " + *t);
            return std::nullopt;
        }
        return v;
    }
    double NumberOr(const std::string& key, double fallback)
    {
        const auto v = Number(key);
        return v ? *v : fallback;
    }
    std::optional<bool> Flag(const std::string& key)
    {
        const std::string* t = Text(key);
        if (t == nullptr) {
            return std::nullopt;
        }
        bool v = false;
        if (!ParseFlag(*t, v)) {
            Bad("bad bool for " + key + ": " + *t);
            return std::nullopt;
        }
        return v;
    }
    // Rejects any key that is not in `allowed`.
    void Allow(std::initializer_list<const char*> allowed)
    {
        for (const auto& [k, v] : m_values) {
            bool known = false;
            for (const char* a : allowed) {
                known = known || k == a;
            }
            if (!known) {
                Bad("unknown option: " + k);
            }
        }
    }
    void Bad(const std::string& message)
    {
        if (m_error.empty()) {
            m_error = message;
        }
    }
    bool Ok() const { return m_error.empty(); }
    const std::string& Error() const { return m_error; }

private:
    std::map<std::string, std::string> m_values;
    std::string m_error;
};

std::vector<std::vector<double>> ParseTuples(const std::string& text, std::size_t width, std::string& error)
{
    std::vector<std::vector<double>> out;
    std::stringstream rows(text);
    std::string row;
    while (std::getline(rows, row, ';')) {
        if (row.empty()) {
            continue;
        }
        std::vector<double> values;
        std::stringstream cells(row);
        std::string cell;
        while (std::getline(cells, cell, ',')) {
            double v = 0.0;
            if (!ParseNumber(cell, v)) {
                error = "bad number in points: " + cell;
                return {};
            }
            values.push_back(v);
        }
        if (values.size() != width) {
            error = "each point needs " + std::to_string(width) + " numbers";
            return {};
        }
        out.push_back(std::move(values));
    }
    return out;
}

std::string BodySummary(const water::WaterBody& b)
{
    return "{\"id\":" + std::to_string(b.id) + ",\"name\":\"" + JsonEscape(b.name) + "\",\"kind\":\"" + water::WaterBodyKindName(b.kind)
        + "\",\"level\":" + std::to_string(b.level) + "}";
}

TerrainCommandResult StateCommand(water::IWaterWorld& world)
{
    const water::WaterStats s = world.Stats();
    std::string body = "{\"success\":true,\"revision\":" + std::to_string(s.revision) + ",\"bodyCount\":" + std::to_string(s.bodyCount)
        + ",\"bodiesHash\":" + std::to_string(s.bodiesHash) + ",\"carveHash\":" + std::to_string(s.carveHash)
        + ",\"carvePending\":" + (s.carvePending ? "true" : "false") + ",\"carveApplies\":" + std::to_string(s.carveApplies)
        + ",\"carveDirtyRects\":" + std::to_string(s.carveDirtyRects) + ",\"featureConnected\":" + (s.featureConnected ? "true" : "false")
        + ",\"featurePushes\":" + std::to_string(s.featurePushes) + ",\"oceanQuadHidden\":" + (s.oceanQuadHidden ? "true" : "false")
        + ",\"carveDebounce\":" + std::to_string(world.CarveDebounce()) + ",\"issues\":[";
    for (std::size_t i = 0; i < s.issues.size(); ++i) {
        body += (i ? ",\"" : "\"") + JsonEscape(s.issues[i]) + "\"";
    }
    body += "],\"lastError\":\"" + JsonEscape(s.lastError) + "\",\"bodies\":[";
    const auto set = world.Bodies();
    for (std::size_t i = 0; i < set->Bodies().size(); ++i) {
        body += (i ? "," : "") + BodySummary(set->Bodies()[i]);
    }
    body += "],\"settings\":" + water::SettingsToJson(world.Settings()) + "}";
    return Json(body);
}

TerrainCommandResult SetCommand(water::IWaterWorld& world, const Pairs& params)
{
    water::WaterSettings next = world.Settings();
    std::optional<double> debounce;
    for (const auto& [key, value] : params) {
        if (key == "carveDebounce") {
            double v = 0.0;
            if (!ParseNumber(value, v)) {
                return Fail(400, "bad number for carveDebounce");
            }
            debounce = v;
            continue;
        }
        if (key == "quality") {
            water::WaterQuality q = water::WaterQuality::Medium;
            if (!water::TryParseWaterQuality(value, q)) {
                return Fail(400, "quality must be low, medium or high");
            }
            next = water::ApplyQuality(next, q);
            continue;
        }
        std::string error;
        if (!water::SetSettingByName(next, key, value, error)) {
            return Fail(400, error);
        }
    }
    if (debounce) {
        world.SetCarveDebounce(*debounce);
    }
    return Done(world.SetSettings(next));
}

void ReadCommonBodyOptions(Query& q, water::WaterBody& b)
{
    if (const std::string* name = q.Text("name")) {
        b.name = *name;
    }
    if (const auto v = q.Number("level")) { b.level = static_cast<float>(*v); }
    if (const auto v = q.Number("depth")) { b.depth = static_cast<float>(*v); }
    if (const auto v = q.Number("shoreWidth")) { b.shoreWidth = static_cast<float>(*v); }
    if (const auto v = q.Number("bankWidth")) { b.bankWidth = static_cast<float>(*v); }
    if (const auto v = q.Number("rimHeight")) { b.rimHeight = static_cast<float>(*v); }
    if (const auto v = q.Number("waveScale")) { b.look.waveScale = static_cast<float>(*v); }
    if (const auto v = q.Flag("carve")) { b.carveTerrain = *v; }
    if (const auto v = q.Flag("seal")) { b.sealRim = *v; }
}

TerrainCommandResult AddLake(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "name", "cx", "cz", "sx", "sz", "points", "level", "depth", "shoreWidth", "bankWidth", "rimHeight", "waveScale", "carve", "seal", "auto_level" });
    water::WaterBody b = water::MakeBoxLake("", 0.0, 0.0, 100.0, 100.0, 0.0f);
    if (const std::string* points = q.Text("points")) {
        std::string error;
        const auto tuples = ParseTuples(*points, 2, error);
        if (!error.empty()) {
            return Fail(400, error);
        }
        b.polygon.clear();
        for (const auto& t : tuples) {
            b.polygon.push_back({ t[0], t[1] });
        }
    } else {
        b.polygon = water::MakeBoxPolygon(q.NumberOr("cx", 0.0), q.NumberOr("cz", 0.0), q.NumberOr("sx", 100.0), q.NumberOr("sz", 100.0));
    }
    ReadCommonBodyOptions(q, b);
    const auto autoLevel = q.Flag("auto_level");
    if (!q.Ok()) {
        return Fail(400, q.Error());
    }
    if (autoLevel.value_or(false)) {
        b.level = water::SuggestLakeLevel(b, [&world](double x, double z) {
            const auto h = world.TerrainHeight(x, z);
            return h ? static_cast<float>(h->baseHeight) : 0.0f;
        });
    }
    return Done(world.AddBody(b));
}

TerrainCommandResult AddRiver(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "name", "points", "width", "depth", "bankWidth", "waveScale", "carve", "level", "auto_level", "freeboard" });
    const std::string* points = q.Text("points");
    if (points == nullptr) {
        return Fail(400, "add_river needs points=x,y,z,width;x,y,z,width;... (width optional: x,y,z)");
    }
    std::string error;
    const double defaultWidth = q.NumberOr("width", 12.0);
    std::vector<water::RiverPoint> pts;
    {
        const auto four = ParseTuples(*points, 4, error);
        if (error.empty()) {
            for (const auto& t : four) {
                pts.push_back({ t[0], t[2], static_cast<float>(t[1]), static_cast<float>(t[3]) });
            }
        } else {
            error.clear();
            for (const auto& t : ParseTuples(*points, 3, error)) {
                pts.push_back({ t[0], t[2], static_cast<float>(t[1]), static_cast<float>(defaultWidth) });
            }
        }
    }
    if (!error.empty()) {
        return Fail(400, error);
    }
    water::WaterBody b = water::MakeRiverBody("", std::move(pts));
    ReadCommonBodyOptions(q, b);
    const auto autoLevel = q.Flag("auto_level");
    const double freeboard = q.NumberOr("freeboard", 1.0);
    if (!q.Ok()) {
        return Fail(400, q.Error());
    }
    if (autoLevel.value_or(false)) {
        water::AutoRiverLevels(b, [&world](double x, double z) {
            const auto h = world.TerrainHeight(x, z);
            return h ? static_cast<float>(h->baseHeight) : 0.0f;
        }, static_cast<float>(freeboard));
    }
    return Done(world.AddBody(b));
}

TerrainCommandResult SetBody(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "id", "name", "level", "depth", "shoreWidth", "bankWidth", "rimHeight", "waveScale", "carve", "seal" });
    const auto id = q.Number("id");
    if (!id) {
        return Fail(400, "set_body needs id");
    }
    const auto set = world.Bodies();
    const water::WaterBody* current = set->Find(static_cast<std::uint32_t>(*id));
    if (current == nullptr) {
        return Fail(409, "no water body with that id");
    }
    water::WaterBody b = *current;
    ReadCommonBodyOptions(q, b);
    if (!q.Ok()) {
        return Fail(400, q.Error());
    }
    return Done(world.UpdateBody(b));
}

TerrainCommandResult PointCommand(water::IWaterWorld& world, const Pairs& params, const std::string& action)
{
    Query q(params);
    q.Allow({ "id", "index", "x", "z", "y", "w" });
    const auto id = q.Number("id");
    if (!id) {
        return Fail(400, action + " needs id");
    }
    const int index = static_cast<int>(q.NumberOr("index", -1.0));
    const auto id32 = static_cast<std::uint32_t>(*id);
    if (action == "remove_point") {
        return q.Ok() ? Done(world.RemovePoint(id32, index)) : Fail(400, q.Error());
    }
    const auto x = q.Number("x");
    const auto z = q.Number("z");
    if (!x || !z) {
        return Fail(400, action + " needs x and z");
    }
    const auto y = q.Number("y");
    const auto w = q.Number("w");
    if (!q.Ok()) {
        return Fail(400, q.Error());
    }
    if (action == "add_point") {
        return Done(world.InsertPoint(id32, index, *x, *z));
    }
    if (index < 0) {
        return Fail(400, "move_point needs index");
    }
    return Done(world.MovePoint(id32, index, *x, *z, y ? std::optional<float>(static_cast<float>(*y)) : std::nullopt,
        w ? std::optional<float>(static_cast<float>(*w)) : std::nullopt));
}

TerrainCommandResult ProbeCommand(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "x", "z" });
    const auto x = q.Number("x");
    const auto z = q.Number("z");
    if (!x || !z || !q.Ok()) {
        return Fail(400, q.Ok() ? "probe needs x and z" : q.Error());
    }
    const water::WaterProbeResult p = world.Probe(*x, *z);
    std::string body = "{\"success\":true,\"x\":" + std::to_string(*x) + ",\"z\":" + std::to_string(*z)
        + ",\"terrainKnown\":" + (p.terrainKnown ? "true" : "false");
    if (p.terrainKnown) {
        body += ",\"baseHeight\":" + std::to_string(p.baseHeight) + ",\"carvedHeight\":" + std::to_string(p.carvedHeight)
            + ",\"carveDelta\":" + std::to_string(p.carvedHeight - p.baseHeight);
    }
    body += std::string(",\"hasWater\":") + (p.water.hasWater ? "true" : "false");
    if (p.water.hasWater) {
        body += ",\"bodyId\":" + std::to_string(p.water.bodyId) + ",\"kind\":\"" + water::WaterBodyKindName(p.water.kind)
            + "\",\"surfaceY\":" + std::to_string(p.water.surfaceY) + ",\"edgeDistance\":" + std::to_string(p.water.edgeDistance);
    }
    body += ",\"depth\":" + std::to_string(p.depth) + std::string(",\"submerged\":") + (p.submerged ? "true" : "false");
    body += p.shoreDistance ? ",\"shoreDistance\":" + std::to_string(*p.shoreDistance) : std::string(",\"shoreDistance\":null");
    body += "}";
    return Json(body);
}

TerrainCommandResult MapCommand(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "size", "cx", "cz", "range", "outlines", "shore" });
    water::WaterMapView view;
    view.size = static_cast<int>(q.NumberOr("size", 512.0));
    view.centerX = q.NumberOr("cx", 0.0);
    view.centerZ = q.NumberOr("cz", 0.0);
    view.halfRange = q.NumberOr("range", 1000.0);
    water::WaterMapOptions options;
    options.outlines = q.Flag("outlines").value_or(true);
    options.shoreBand = q.Flag("shore").value_or(true);
    if (!q.Ok()) {
        return Fail(400, q.Error());
    }
    if (!world.TerrainHeight(view.centerX, view.centerZ)) {
        return Fail(409, "open the terrain first");
    }
    try {
        const auto set = world.Bodies();
        const water::WaterMapImage image = water::RenderWaterMap(*set, [&world](double x, double z) {
            const auto h = world.TerrainHeight(x, z);
            return h ? static_cast<float>(h->carvedHeight) : 0.0f;
        }, view, options);
        const std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(image.rgba.data(), image.size, image.size);
        TerrainCommandResult result;
        result.contentType = "image/png";
        result.body.assign(reinterpret_cast<const char*>(png.data()), png.size());
        return result;
    } catch (const std::exception& e) {
        return Fail(500, std::string("map failed: ") + e.what());
    }
}

TerrainCommandResult FileCommand(water::IWaterWorld& world, const Pairs& params, bool save)
{
    Query q(params);
    q.Allow({ "path" });
    const std::string* path = q.Text("path");
    if (path == nullptr || path->empty()) {
        return Fail(400, std::string(save ? "save" : "load") + " needs path=...");
    }
    return Done(save ? world.SaveFile(*path) : world.LoadFile(*path));
}

TerrainCommandResult LeakCommand(water::IWaterWorld& world, const Pairs& params)
{
    Query q(params);
    q.Allow({ "id" });
    const auto id = q.Number("id");
    if (!id || !q.Ok()) {
        return Fail(400, q.Ok() ? "leaks needs id" : q.Error());
    }
    const water::WaterBody* b = world.Bodies()->Find(static_cast<std::uint32_t>(*id));
    if (b == nullptr || b->kind != water::WaterBodyKind::Lake) {
        return Fail(409, "id must name a lake");
    }
    const auto ground = [&world](double x, double z) {
        const auto h = world.TerrainHeight(x, z);
        return h ? static_cast<float>(h->baseHeight) : 0.0f;
    };
    const water::LakeLeakReport r = water::CheckLakeLeaks(*b, ground);
    return Json("{\"success\":true,\"samples\":" + std::to_string(r.samples) + ",\"below\":" + std::to_string(r.below)
        + ",\"lowest\":" + std::to_string(r.lowest) + ",\"highest\":" + std::to_string(r.highest) + ",\"level\":" + std::to_string(b->level)
        + ",\"suggestedLevel\":" + std::to_string(water::SuggestLakeLevel(*b, ground)) + "}");
}

} // namespace

TerrainCommandResult ExecuteWaterCommand(water::IWaterWorld& world, const TerrainCommandRequest& request)
{
    const std::string action = request.action.rfind("water.", 0) == 0 ? request.action.substr(6) : request.action;
    const Pairs& params = request.params;
    if (action == "state") {
        return StateCommand(world);
    }
    if (action == "list") {
        return Json(water::WorldToJson(*world.Bodies()));
    }
    if (action == "settings") {
        return Json(water::SettingsToJson(world.Settings()));
    }
    if (action == "set") {
        return SetCommand(world, params);
    }
    if (action == "add_lake") {
        return AddLake(world, params);
    }
    if (action == "add_river") {
        return AddRiver(world, params);
    }
    if (action == "add_ocean") {
        return Done(world.AddBody(water::MakeOceanBody(world.Settings().seaLevel)));
    }
    if (action == "set_body") {
        return SetBody(world, params);
    }
    if (action == "move_point" || action == "add_point" || action == "remove_point") {
        return PointCommand(world, params, action);
    }
    if (action == "remove") {
        Query q(params);
        q.Allow({ "id" });
        const auto id = q.Number("id");
        return id && q.Ok() ? Done(world.RemoveBody(static_cast<std::uint32_t>(*id))) : Fail(400, "remove needs id");
    }
    if (action == "clear") {
        return Done(world.ClearBodies());
    }
    if (action == "flush") {
        world.FlushCarve();
        return Json("{\"success\":true,\"message\":\"carve flushed on the next frame\"}");
    }
    if (action == "probe") {
        return ProbeCommand(world, params);
    }
    if (action == "map") {
        return MapCommand(world, params);
    }
    if (action == "leaks") {
        return LeakCommand(world, params);
    }
    if (action == "save") {
        return FileCommand(world, params, true);
    }
    if (action == "load") {
        return FileCommand(world, params, false);
    }
    return Fail(404, "unknown water action: " + action);
}

} // namespace gte
