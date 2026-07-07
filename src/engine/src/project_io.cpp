#include "velocity/engine/project_io.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace velocity::engine {

using json = nlohmann::json;

namespace {

json toJson(const Clip& c) {
    return json{
        {"asset", reinterpret_cast<const char*>(c.asset.u8string().c_str())},
        {"dstStart", c.dstStart},
        {"dstLen", c.dstLen},
        {"srcStartPts", c.srcStartPts},
        {"srcTimebase", {c.srcTimebase.num, c.srcTimebase.den}},
        {"gain", c.gain},
        {"mute", c.mute},
        {"fadeIn", c.fadeIn},
        {"fadeOut", c.fadeOut},
        {"hidden", c.hidden},
        {"transform",
         {{"posX", c.transform.posX},
          {"posY", c.transform.posY},
          {"scale", c.transform.scale},
          {"rotation", c.transform.rotation},
          {"opacity", c.transform.opacity}}},
    };
}

Clip clipFromJson(const json& j) {
    Clip c;
    c.id = nextClipId(); // ids are session-local, not persisted identity
    const std::string assetUtf8 = j.at("asset").get<std::string>();
    c.asset = std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(assetUtf8.c_str())));
    c.dstStart = j.at("dstStart").get<Tick>();
    c.dstLen = j.at("dstLen").get<Tick>();
    c.srcStartPts = j.at("srcStartPts").get<std::int64_t>();
    c.srcTimebase = {j.at("srcTimebase").at(0).get<std::int64_t>(),
                     j.at("srcTimebase").at(1).get<std::int64_t>()};
    c.gain = j.value("gain", 1.0f);
    c.mute = j.value("mute", false);
    c.fadeIn = j.value("fadeIn", Tick{0});
    c.fadeOut = j.value("fadeOut", Tick{0});
    c.hidden = j.value("hidden", false);
    if (auto t = j.find("transform"); t != j.end()) {
        c.transform.posX = t->value("posX", 0.0f);
        c.transform.posY = t->value("posY", 0.0f);
        c.transform.scale = t->value("scale", 1.0f);
        c.transform.rotation = t->value("rotation", 0.0f);
        c.transform.opacity = t->value("opacity", 1.0f);
    }
    return c;
}

} // namespace

std::string serializeProject(const Sequence& seq) {
    json tracks = json::array();
    for (const auto& track : seq.tracks) {
        json clips = json::array();
        for (const auto& c : track->clips)
            clips.push_back(toJson(*c));
        tracks.push_back(json{
            {"kind", track->kind == TrackKind::video ? "video" : "audio"},
            {"name", track->name},
            {"clips", std::move(clips)},
        });
    }

    const json doc{
        {"velocity_project", kProjectSchemaVersion},
        {"sequence",
         {{"width", seq.width},
          {"height", seq.height},
          {"frameRate", {seq.frameRate.num, seq.frameRate.den}},
          {"audioRate", seq.audioRate},
          {"tracks", std::move(tracks)}}},
    };
    return doc.dump(2);
}

Expected<SnapshotPtr, std::string> deserializeProject(const std::string& text) {
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded())
        return makeUnexpected(std::string("not a valid Velocity project (JSON parse error)"));
    if (!doc.contains("velocity_project"))
        return makeUnexpected(std::string("not a Velocity project file"));
    if (doc["velocity_project"].get<int>() > kProjectSchemaVersion)
        return makeUnexpected(
            std::string("project was saved by a newer Velocity version"));

    // Field access below uses .at(), which throws on malformed documents —
    // catch and translate (docs/13: media/engine surface errors as values).
    try {
        const json& s = doc.at("sequence");
        auto seq = std::make_shared<Sequence>();
        seq->width = s.at("width").get<int>();
        seq->height = s.at("height").get<int>();
        seq->frameRate = {s.at("frameRate").at(0).get<std::int64_t>(),
                          s.at("frameRate").at(1).get<std::int64_t>()};
        seq->audioRate = s.value("audioRate", 48000);

        for (const json& jt : s.at("tracks")) {
            auto track = std::make_shared<Track>();
            track->kind =
                jt.at("kind").get<std::string>() == "video" ? TrackKind::video : TrackKind::audio;
            track->name = jt.value("name", std::string{});
            for (const json& jc : jt.at("clips"))
                track->clips.push_back(std::make_shared<const Clip>(clipFromJson(jc)));
            // Restore invariant: sorted, non-overlapping.
            std::sort(track->clips.begin(), track->clips.end(),
                      [](const ClipPtr& a, const ClipPtr& b) { return a->dstStart < b->dstStart; });
            for (size_t i = 1; i < track->clips.size(); ++i)
                if (track->clips[i]->dstStart < track->clips[i - 1]->dstEnd())
                    return makeUnexpected(std::string("project contains overlapping clips"));
            seq->tracks.push_back(std::move(track));
        }
        return SnapshotPtr{std::move(seq)};
    } catch (const json::exception& e) {
        return makeUnexpected(std::string("malformed project: ") + e.what());
    }
}

Expected<void, std::string> saveProject(const SnapshotPtr& seq,
                                        const std::filesystem::path& file) {
    // Write to a temp sibling then rename: never truncate a good project
    // file with a failed write (docs/03 durability intent).
    const auto tmp = std::filesystem::path(file).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return makeUnexpected("cannot write " + file.string());
        const std::string text = serializeProject(*seq);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good())
            return makeUnexpected("write failed for " + file.string());
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        // rename can't replace across some conditions; try remove+rename
        std::filesystem::remove(file, ec);
        std::filesystem::rename(tmp, file, ec);
        if (ec)
            return makeUnexpected("cannot replace " + file.string() + ": " + ec.message());
    }
    return {};
}

Expected<SnapshotPtr, std::string> loadProject(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return makeUnexpected("cannot open " + file.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return deserializeProject(text);
}

} // namespace velocity::engine
