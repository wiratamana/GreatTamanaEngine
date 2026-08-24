#include "MotionFile.h"

#include <cstring>

namespace gte {

namespace {

// --- Binary writer ---------------------------------------------------------
// Same small, local "append-only byte writer" shape as RigFile.cpp's own
// BinaryWriter - duplicated here rather than shared, matching this engine's
// existing convention for such small, format-specific helpers (see
// RigFile.cpp's own comment on why). Adds WriteRawBytes() beyond RigFile.cpp's
// set, for BoneKeyframe::interpolation/CameraKeyframe::interpolation's fixed
// raw byte arrays.
class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<std::uint8_t>& bytes) : m_bytes(bytes) { }

    void U8(std::uint8_t v) { m_bytes.push_back(v); }
    void Bool(bool v) { U8(v ? 1 : 0); }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            U8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void F32(float v)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void String(const std::string& s)
    {
        U32(static_cast<std::uint32_t>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }

    void Vec3Val(const Vec3& v)
    {
        F32(v.x);
        F32(v.y);
        F32(v.z);
    }

    void QuatVal(const Quat& q)
    {
        F32(q.x);
        F32(q.y);
        F32(q.z);
        F32(q.w);
    }

    template <std::size_t N> void RawBytes(const std::array<std::uint8_t, N>& raw)
    {
        m_bytes.insert(m_bytes.end(), raw.begin(), raw.end());
    }

private:
    std::vector<std::uint8_t>& m_bytes;
};

// --- Binary reader ---------------------------------------------------------
// Mirrors BinaryWriter above - same "sticky failure" convention as
// RigFile.cpp's own BinaryReader (see that file's own comment): once any
// Read* call runs past the end of `m_bytes`, every subsequent Read* call
// becomes a no-op and Ok() latches false.
class BinaryReader {
public:
    BinaryReader(const std::vector<std::uint8_t>& bytes, std::size_t cursor) : m_bytes(bytes), m_cursor(cursor) { }

    bool Ok() const noexcept { return m_ok; }

    std::uint8_t U8()
    {
        if (!EnsureAvailable(1)) {
            return 0;
        }
        return m_bytes[m_cursor++];
    }

    bool Bool() { return U8() != 0; }

    std::uint32_t U32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(U8()) << (8 * i);
        }
        return v;
    }

    float F32()
    {
        const std::uint32_t bits = U32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    std::string String()
    {
        const std::uint32_t length = U32();
        if (!EnsureAvailable(length)) {
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_cursor), length);
        m_cursor += length;
        return s;
    }

    Vec3 Vec3Val()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        return Vec3(x, y, z);
    }

    Quat QuatVal()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        const float w = F32();
        return Quat(x, y, z, w);
    }

    template <std::size_t N> std::array<std::uint8_t, N> RawBytes()
    {
        std::array<std::uint8_t, N> out{};
        if (!EnsureAvailable(N)) {
            return out;
        }
        std::memcpy(out.data(), m_bytes.data() + m_cursor, N);
        m_cursor += N;
        return out;
    }

private:
    bool EnsureAvailable(std::size_t count)
    {
        if (!m_ok || m_cursor + count > m_bytes.size()) {
            m_ok = false;
            return false;
        }
        return true;
    }

    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_cursor;
    bool m_ok = true;
};

void WriteBoneKeyframes(BinaryWriter& w, const std::vector<BoneKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.String(kf.boneName);
        w.U32(kf.frame);
        w.Vec3Val(kf.translation);
        w.QuatVal(kf.rotation);
        w.RawBytes(kf.interpolation);
    }
}

bool ReadBoneKeyframes(BinaryReader& r, std::vector<BoneKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        BoneKeyframe kf;
        kf.boneName = r.String();
        kf.frame = r.U32();
        kf.translation = r.Vec3Val();
        kf.rotation = r.QuatVal();
        kf.interpolation = r.RawBytes<64>();
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

void WriteMorphKeyframes(BinaryWriter& w, const std::vector<MorphKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.String(kf.morphName);
        w.U32(kf.frame);
        w.F32(kf.weight);
    }
}

bool ReadMorphKeyframes(BinaryReader& r, std::vector<MorphKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        MorphKeyframe kf;
        kf.morphName = r.String();
        kf.frame = r.U32();
        kf.weight = r.F32();
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

void WriteCameraKeyframes(BinaryWriter& w, const std::vector<CameraKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.U32(kf.frame);
        w.F32(kf.distance);
        w.Vec3Val(kf.interest);
        w.Vec3Val(kf.rotateRadians);
        w.RawBytes(kf.interpolation);
        w.U32(kf.fieldOfViewDegrees);
        w.Bool(kf.isPerspective);
    }
}

bool ReadCameraKeyframes(BinaryReader& r, std::vector<CameraKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        CameraKeyframe kf;
        kf.frame = r.U32();
        kf.distance = r.F32();
        kf.interest = r.Vec3Val();
        kf.rotateRadians = r.Vec3Val();
        kf.interpolation = r.RawBytes<24>();
        kf.fieldOfViewDegrees = r.U32();
        kf.isPerspective = r.Bool();
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

void WriteLightKeyframes(BinaryWriter& w, const std::vector<LightKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.U32(kf.frame);
        w.Vec3Val(kf.color);
        w.Vec3Val(kf.direction);
    }
}

bool ReadLightKeyframes(BinaryReader& r, std::vector<LightKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        LightKeyframe kf;
        kf.frame = r.U32();
        kf.color = r.Vec3Val();
        kf.direction = r.Vec3Val();
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

void WriteShadowKeyframes(BinaryWriter& w, const std::vector<ShadowKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.U32(kf.frame);
        w.U8(kf.shadowType);
        w.F32(kf.distance);
    }
}

bool ReadShadowKeyframes(BinaryReader& r, std::vector<ShadowKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        ShadowKeyframe kf;
        kf.frame = r.U32();
        kf.shadowType = r.U8();
        kf.distance = r.F32();
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

void WriteIkKeyframes(BinaryWriter& w, const std::vector<IkKeyframe>& keyframes)
{
    w.U32(static_cast<std::uint32_t>(keyframes.size()));
    for (const auto& kf : keyframes) {
        w.U32(kf.frame);
        w.Bool(kf.visible);
        w.U32(static_cast<std::uint32_t>(kf.states.size()));
        for (const auto& state : kf.states) {
            w.String(state.ikBoneName);
            w.Bool(state.enabled);
        }
    }
}

bool ReadIkKeyframes(BinaryReader& r, std::vector<IkKeyframe>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        IkKeyframe kf;
        kf.frame = r.U32();
        kf.visible = r.Bool();
        const std::uint32_t stateCount = r.U32();
        kf.states.reserve(stateCount);
        for (std::uint32_t j = 0; j < stateCount && r.Ok(); ++j) {
            IkEnableState state;
            state.ikBoneName = r.String();
            state.enabled = r.Bool();
            kf.states.push_back(std::move(state));
        }
        out->push_back(std::move(kf));
    }
    return r.Ok();
}

} // namespace

std::vector<std::uint8_t> EncodeMotionDataToBytes(const MotionData& motion)
{
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kMotionFileMagic, kMotionFileMagic + sizeof(kMotionFileMagic));

    BinaryWriter w(bytes);
    w.String(motion.modelName);
    WriteBoneKeyframes(w, motion.boneKeyframes);
    WriteMorphKeyframes(w, motion.morphKeyframes);
    WriteCameraKeyframes(w, motion.cameraKeyframes);
    WriteLightKeyframes(w, motion.lightKeyframes);
    WriteShadowKeyframes(w, motion.shadowKeyframes);
    WriteIkKeyframes(w, motion.ikKeyframes);

    return bytes;
}

std::optional<MotionData> DecodeMotionDataFromBytes(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < sizeof(kMotionFileMagic)) {
        return std::nullopt;
    }
    if (std::memcmp(bytes.data(), kMotionFileMagic, sizeof(kMotionFileMagic)) != 0) {
        return std::nullopt;
    }

    BinaryReader r(bytes, sizeof(kMotionFileMagic));

    MotionData motion;
    motion.modelName = r.String();
    if (!r.Ok()) {
        return std::nullopt;
    }
    if (!ReadBoneKeyframes(r, &motion.boneKeyframes)) {
        return std::nullopt;
    }
    if (!ReadMorphKeyframes(r, &motion.morphKeyframes)) {
        return std::nullopt;
    }
    if (!ReadCameraKeyframes(r, &motion.cameraKeyframes)) {
        return std::nullopt;
    }
    if (!ReadLightKeyframes(r, &motion.lightKeyframes)) {
        return std::nullopt;
    }
    if (!ReadShadowKeyframes(r, &motion.shadowKeyframes)) {
        return std::nullopt;
    }
    if (!ReadIkKeyframes(r, &motion.ikKeyframes)) {
        return std::nullopt;
    }

    return motion;
}

} // namespace gte
