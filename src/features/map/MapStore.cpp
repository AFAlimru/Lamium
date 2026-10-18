#include "features/map/MapStore.h"
#include "app/AtomicFile.h"
#include "app/Runtime.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

namespace lamium::map::store {
namespace {
struct RegionId {
    MapLayer layer;
    RegionKey key;
    auto order() const { return std::tuple(layer.dimension, layer.band, key.x, key.z); }
    bool operator<(RegionId const& other) const { return order() < other.order(); }
    bool operator==(RegionId const& other) const { return order() == other.order(); }
};
struct ImageId {
    MapLayer layer;
    TileKey tile;
    auto order() const { return std::tuple(layer.dimension, layer.band, tile.lod, tile.x, tile.z); }
    bool operator<(ImageId const& other) const { return order() < other.order(); }
    bool operator==(ImageId const& other) const { return order() == other.order(); }
};
struct Resident {
    std::shared_ptr<RegionData> data = std::make_shared<RegionData>();
    // Columns the scan wrote since this region was created; the saved file
    // fills only the others when it arrives.
    std::vector<bool> recorded = std::vector<bool>(regionColumns, false);
    bool ready = false; // The saved file has been merged (or there is none).
    bool dirty = false;
    double dirtySince = 0, used = 0;
};
using Pixels = std::shared_ptr<std::vector<std::uint32_t> const>;
struct Cached {
    Pixels pixels;
    unsigned version = 0;
    bool stale = false; // The regions under it changed; shown until rebuilt.
    double builtAt = 0, used = 0;
};
struct Save {
    std::filesystem::path path;
    std::shared_ptr<RegionData const> data;
};

std::mutex mutex;
std::condition_variable wake;
std::thread worker;
bool finishing = false; // World exit or stop: save what changed, then end.
// Bumped when the world changes or its map is cleared: work started before
// is dropped when it finishes.
unsigned generation = 0;
unsigned epochs = 0;
std::optional<unsigned> attachedWorld;
std::optional<std::filesystem::path> root;
std::map<RegionId, Resident> residents;
std::map<ImageId, Cached> images;
std::deque<Save> saves;
std::deque<RegionId> loads;
std::vector<ImageId> wanted;
// Saved and recorded regions per layer (dimension, band). Images wait for
// the saved folder's listing so they never miss a region.
std::map<std::pair<int, int>, ExploredIndex> explored;
bool indexing = false;
// Coarse images whose regions changed since they were built, cached or
// not: a rebuild keeps the other quarters from the previous image.
std::set<ImageId> dirty;
std::optional<std::filesystem::path> removing;
std::optional<std::uint64_t> usageBytes;
bool usageDirty = false;
unsigned versions = 0;
double clock = 0;
std::map<ImageId, std::array<unsigned, 4>> builtFrom; // Worker side: the child versions each coarse image was made from.
bool missingTurn = true; // Worker only: never-built and stale images take turns (L-215).
int logged = 0;
ll::event::ListenerPtr exitListener;

// About 40 MB of images; tiles not wanted now go first, finest and oldest
// first, since coarse ones are the costly ones to build again.
constexpr size_t maxImages = 160;
constexpr double saveDelay = 10, rebuildDelay = 1.5, forgetAfter = 20;
constexpr int keepRegions = 2; // Around the player: the minimap reads them.

void log(std::string const& text) {
    // Bounded: the log is for the first runs of an experimental feature.
    if (++logged > 30) return;
    try { Runtime::instance().self().getLogger().info("World map: {}", text); } catch (...) {}
}
// Why an image with recorded land under it came out without it (L-215):
// bounded, for chasing the black areas seen while moving.
int traced = 0;
void trace(ImageId const& id, std::string_view why) {
    if (++traced > 150) return;
    try {
        Runtime::instance().self().getLogger().info("World map image {} {} {}: {}", id.tile.lod, id.tile.x, id.tile.z, why);
    } catch (...) {}
}
std::filesystem::path regionPath(std::filesystem::path const& base, RegionId const& id) {
    return base / std::filesystem::u8path(layerFolder(id.layer)) / regionFileName(id.key);
}
std::optional<RegionData> readRegion(std::filesystem::path const& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto region = decodeRegion(bytes);
    // A damaged file is treated as missing; the next save replaces it.
    if (!region) log("ignored an unreadable region file " + path.filename().string());
    return region;
}
void invalidate(RegionId const& id) {
    for (int lod = 0; lod <= maxLod; ++lod) {
        ImageId image{id.layer, {lod, id.key.x >> lod, id.key.z >> lod}};
        auto found = images.find(image);
        if (found != images.end()) found->second.stale = true;
        dirty.insert(image);
    }
}
ExploredIndex& indexOf(MapLayer layer) { return explored[{layer.dimension, layer.band}]; }
bool known(ImageId const& id) {
    auto found = explored.find({id.layer.dimension, id.layer.band});
    return found != explored.end() && found->second.has(id.tile);
}
// Caller holds the lock.
Resident& resident(RegionId const& id) {
    auto [at, created] = residents.try_emplace(id);
    if (created) {
        if (root) loads.push_back(id);
        else at->second.ready = true;
        wake.notify_one();
    }
    at->second.used = clock;
    return at->second;
}
void queueSave(RegionId const& id, Resident& r) {
    if (!root || !r.ready || !r.dirty) return;
    saves.push_back({regionPath(*root, id), r.data});
    r.dirty = false;
    wake.notify_one();
}
bool fresh(Cached const& cached) { return !cached.stale; }
bool buildable(ImageId const& id) {
    auto found = images.find(id);
    // Dirty too: a change that came in while the image was being built.
    return found == images.end()
        || ((found->second.stale || dirty.contains(id)) && clock - found->second.builtAt >= rebuildDelay);
}
void trimImages();

// Worker side. Builds without the lock; takes it only to read sources and to
// publish. Results from an older generation are dropped.
Pixels build(ImageId const& id, unsigned started);
// A child image and its version (0 when it could not be made).
std::pair<Pixels, unsigned> child(ImageId const& id, unsigned started) {
    {
        std::lock_guard lock(mutex);
        if (started != generation || finishing || !known(id)) return {nullptr, 0};
        auto found = images.find(id);
        if (found != images.end() && fresh(found->second) && !dirty.contains(id)) {
            found->second.used = clock;
            return {found->second.pixels, found->second.version};
        }
    }
    auto pixels = build(id, started);
    std::lock_guard lock(mutex);
    if (started != generation) return {nullptr, 0};
    auto version = ++versions;
    images[id] = Cached{pixels, version, false, clock, clock};
    // A far view builds every image under it; keep memory bounded meanwhile.
    trimImages();
    return {pixels, pixels ? version : 0u};
}
Pixels build(ImageId const& id, unsigned started) {
    if (id.tile.lod == 0) {
        std::shared_ptr<RegionData const> source;
        std::optional<std::filesystem::path> path;
        {
            std::lock_guard lock(mutex);
            if (started != generation || !known(id)) return nullptr;
            dirty.erase(id);
            RegionId region{id.layer, {id.tile.x, id.tile.z}};
            auto found = residents.find(region);
            if (found != residents.end() && found->second.ready) source = found->second.data;
            else if (root) path = regionPath(*root, region);
            if (!source && found != residents.end()) trace(id, "region still loading, read from its file");
        }
        std::optional<RegionData> read;
        if (!source && path) {
            read = readRegion(*path);
            if (!read) {
                trace(id, "no region file to read");
                return nullptr;
            }
        }
        auto const& data = source ? *source : *read;
        if (data.empty()) {
            trace(id, source ? "resident region is empty" : "region file is empty");
            return nullptr;
        }
        return std::make_shared<std::vector<std::uint32_t> const>(shadeRegion(data));
    }
    // Quarters whose child image did not change are kept from the last
    // image, so a change near the player rebuilds one path down, not the
    // whole area. "Did not change" is the child's version this image was
    // built from: a child rebuilt on its own first (viewed at a finer scale)
    // had lost its dirty mark, and the coarse image kept the old quarter for
    // good (L-215, the area by the player stayed black at coarse scales).
    Pixels previous;
    std::array<bool, 4> keep{}, recorded{};
    std::array<unsigned, 4> from{}, made{};
    auto below = [&](int q) { return ImageId{id.layer, {id.tile.lod - 1, id.tile.x * 2 + (q & 1), id.tile.z * 2 + (q >> 1)}}; };
    {
        std::lock_guard lock(mutex);
        if (started != generation) return nullptr;
        dirty.erase(id);
        auto found = images.find(id);
        if (found != images.end()) previous = found->second.pixels;
        if (auto sources = builtFrom.find(id); previous && sources != builtFrom.end()) from = sources->second;
        for (int q = 0; q < 4; ++q) {
            auto i = static_cast<size_t>(q);
            recorded[i] = known(below(q));
            auto current = images.find(below(q));
            bool currentFresh = current != images.end() && fresh(current->second) && !dirty.contains(below(q));
            keep[i] = quarterUnchanged(previous != nullptr, recorded[i], from[i],
                                       current != images.end() ? current->second.version : 0u, currentFresh);
        }
    }
    auto pixels = previous ? *previous : std::vector<std::uint32_t>(static_cast<size_t>(regionColumns), 0);
    bool retry = false;
    for (int q = 0; q < 4; ++q) {
        auto i = static_cast<size_t>(q);
        if (keep[i]) {
            made[i] = from[i];
            continue;
        }
        auto [part, version] = child(below(q), started);
        switch (quarterAction(false, part != nullptr, recorded[i], previous != nullptr)) {
        case Quarter::Draw:
            downsampleInto(pixels, *part, q);
            made[i] = version;
            break;
        case Quarter::KeepAndRetry:
            retry = true;
            trace(below(q), "not made now; its old quarter is kept and the parent retried");
            break;
        case Quarter::Clear: clearQuadrant(pixels, q); break;
        case Quarter::Keep: break;
        }
    }
    {
        std::lock_guard lock(mutex);
        if (started == generation) {
            if (retry) dirty.insert(id);
            builtFrom[id] = made;
        }
    }
    if (std::none_of(pixels.begin(), pixels.end(), [](std::uint32_t c) { return c >> 24; })) {
        if (std::any_of(recorded.begin(), recorded.end(), [](bool r) { return r; }))
            trace(id, retry ? "empty for now, retried" : "built empty though recorded land is under it");
        return nullptr;
    }
    return std::make_shared<std::vector<std::uint32_t> const>(std::move(pixels));
}
void trimImages() {
    if (images.size() <= maxImages) return;
    std::vector<std::pair<double, ImageId>> order;
    for (auto const& [id, cached] : images)
        if (std::find(wanted.begin(), wanted.end(), id) == wanted.end()) order.push_back({cached.used, id});
    std::sort(order.begin(), order.end(), [](auto const& a, auto const& b) {
        return std::pair(a.second.tile.lod, a.first) < std::pair(b.second.tile.lod, b.first);
    });
    for (size_t i = 0; i < order.size() && images.size() > maxImages; ++i) {
        images.erase(order[i].second);
        builtFrom.erase(order[i].second);
    }
}
std::uint64_t folderSize(std::filesystem::path const& folder) {
    std::uint64_t total = 0;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error)) total += it->file_size(error);
    return total;
}
void run() {
    std::unique_lock lock(mutex);
    for (;;) {
        wake.wait(lock, [] {
            bool loadsWanted = !loads.empty() && (!finishing || std::any_of(loads.begin(), loads.end(), [](auto const& id) {
                auto found = residents.find(id);
                return found != residents.end() && found->second.dirty;
            }));
            bool builds = !finishing && !indexing && std::any_of(wanted.begin(), wanted.end(), buildable);
            return removing || (indexing && !finishing) || !saves.empty() || loadsWanted || builds || (usageDirty && !finishing)
                || (finishing && saves.empty() && !loadsWanted);
        });
        if (removing) {
            auto folder = *std::exchange(removing, std::nullopt);
            lock.unlock();
            std::error_code error;
            std::filesystem::remove_all(folder, error);
            if (error) log("could not delete the saved map: " + error.message());
            lock.lock();
            usageDirty = true;
            continue;
        }
        if (indexing && !finishing) {
            auto folder = root;
            unsigned started = generation;
            lock.unlock();
            std::vector<std::pair<MapLayer, RegionKey>> listed;
            std::error_code error;
            if (folder)
                for (auto it = std::filesystem::recursive_directory_iterator(*folder, error);
                     !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                    if (!it->is_regular_file(error)) continue;
                    auto key = regionOfFileName(it->path().filename().string());
                    auto layer = layerOfFolder(it->path().parent_path().lexically_relative(*folder).generic_string());
                    if (key && layer) listed.push_back({*layer, *key});
                }
            lock.lock();
            if (started != generation) continue;
            for (auto const& [layer, key] : listed) indexOf(layer).add(key);
            indexing = false;
            continue;
        }
        if (!saves.empty()) {
            auto save = std::move(saves.front());
            saves.pop_front();
            lock.unlock();
            try { writeFileReplacing(save.path, encodeRegion(*save.data), "map region"); }
            catch (std::exception const& error) { log(std::string("could not save a region: ") + error.what()); }
            lock.lock();
            usageDirty = true;
            continue;
        }
        if (!loads.empty()) {
            auto id = loads.front();
            loads.pop_front();
            auto found = residents.find(id);
            if (found == residents.end() || (finishing && !found->second.dirty) || !root) continue;
            auto path = regionPath(*root, id);
            unsigned started = generation;
            lock.unlock();
            auto disk = readRegion(path);
            lock.lock();
            found = residents.find(id);
            if (started != generation || found == residents.end()) continue;
            auto& r = found->second;
            if (disk) {
                if (r.data.use_count() > 1) r.data = std::make_shared<RegionData>(*r.data);
                underlay(*r.data, r.recorded, *disk);
            }
            r.ready = true;
            invalidate(id);
            if (finishing) queueSave(id, r);
            continue;
        }
        if (finishing) break;
        auto next = nextBuild(wanted, [](ImageId const& id) { return !images.contains(id); }, buildable, missingTurn);
        if (next != wanted.end()) {
            missingTurn = !missingTurn;
            auto id = *next;
            unsigned started = generation;
            lock.unlock();
            Pixels pixels;
            try { pixels = build(id, started); }
            catch (std::exception const& error) { log(std::string("could not build an image: ") + error.what()); }
            lock.lock();
            if (started != generation) continue;
            images[id] = Cached{pixels, ++versions, false, clock, clock};
            trimImages();
            continue;
        }
        if (usageDirty) {
            usageDirty = false;
            auto folder = root;
            lock.unlock();
            std::optional<std::uint64_t> bytes;
            if (folder) bytes = folderSize(*folder);
            lock.lock();
            if (folder == root) usageBytes = bytes;
        }
    }
}
// Caller holds the lock. Saves what changed, then lets the worker end and
// joins it; the lock is released while waiting.
void finish(std::unique_lock<std::mutex>& lock) {
    for (auto& [id, r] : residents) queueSave(id, r);
    finishing = true;
    wanted.clear();
    wake.notify_all();
    if (worker.joinable()) {
        lock.unlock();
        worker.join();
        lock.lock();
    }
    finishing = false;
    ++generation;
    ++epochs;
    attachedWorld.reset();
    root.reset();
    residents.clear();
    images.clear();
    builtFrom.clear();
    explored.clear();
    dirty.clear();
    indexing = false;
    saves.clear();
    loads.clear();
    removing.reset();
    usageBytes.reset();
    usageDirty = false;
}
void detach() {
    std::unique_lock lock(mutex);
    finish(lock);
}
}

void frame(unsigned world, std::optional<std::filesystem::path> const& folder, MapLayer layer, double x, double z,
           double now) {
    std::unique_lock lock(mutex);
    clock = now;
    if (attachedWorld != world) {
        if (attachedWorld) finish(lock);
        attachedWorld = world;
        root = folder;
        usageDirty = root.has_value();
        indexing = root.has_value();
        ++generation;
        ++epochs;
        worker = std::thread(run);
        if (root) log("saving to " + root->string());
        else log("this world is kept for the session only");
    }
    auto center = regionOfBlock(blockFloor(x), blockFloor(z));
    for (int dz = -keepRegions; dz <= keepRegions; ++dz)
        for (int dx = -keepRegions; dx <= keepRegions; ++dx) resident({layer, {center.x + dx, center.z + dz}});
    for (auto it = residents.begin(); it != residents.end();) {
        auto& [id, r] = *it;
        if (r.dirty && now - r.dirtySince >= saveDelay) queueSave(id, r);
        bool near = id.layer == layer && std::abs(id.key.x - center.x) <= keepRegions + 1
            && std::abs(id.key.z - center.z) <= keepRegions + 1;
        // Session-only regions are the only copy; they stay.
        if (root && !near && r.ready && !r.dirty && now - r.used >= forgetAfter) it = residents.erase(it);
        else ++it;
    }
}
void record(MapLayer layer, ChunkKey chunk, std::array<Column, 256> const& columns) {
    std::lock_guard lock(mutex);
    if (!attachedWorld || finishing) return;
    RegionId id{layer, regionOfChunk(chunk)};
    auto& r = resident(id);
    // The worker may be reading this region; it keeps its own copy.
    auto data = r.data.use_count() > 1 ? std::make_shared<RegionData>(*r.data) : r.data;
    if (!mergeChunk(*data, chunk, columns, &r.recorded)) return;
    r.data = std::move(data);
    if (!r.dirty) r.dirtySince = clock;
    r.dirty = true;
    indexOf(layer).add(id.key);
    invalidate(id);
}
bool cached(MapLayer layer, ChunkKey chunk, std::array<Column, 256>& out) {
    std::lock_guard lock(mutex);
    auto found = residents.find({layer, regionOfChunk(chunk)});
    if (found == residents.end() || !found->second.ready) return false;
    found->second.used = clock;
    return copyChunk(*found->second.data, chunk, out);
}
std::optional<Column> column(MapLayer layer, int x, int z) {
    std::lock_guard lock(mutex);
    auto found = residents.find({layer, regionOfBlock(x, z)});
    if (found == residents.end() || !found->second.ready) return std::nullopt;
    auto i = static_cast<size_t>(regionIndex(x, z));
    auto const& data = *found->second.data;
    if (!(data.colors[i] >> 24)) return std::nullopt;
    return Column{data.colors[i], data.heights[i]};
}
std::optional<Image> image(MapLayer layer, TileKey tile) {
    std::lock_guard lock(mutex);
    auto found = images.find({layer, tile});
    if (found == images.end()) return std::nullopt;
    found->second.used = clock;
    return Image{found->second.pixels, found->second.version};
}
void want(MapLayer layer, std::vector<TileKey> tiles) {
    std::lock_guard lock(mutex);
    if (!attachedWorld || finishing) return;
    wanted.clear();
    for (auto tile : tiles) wanted.push_back({layer, tile});
    wake.notify_one();
}
size_t pending() {
    std::lock_guard lock(mutex);
    return static_cast<size_t>(std::count_if(wanted.begin(), wanted.end(), [](ImageId const& id) {
        auto found = images.find(id);
        return found == images.end() || found->second.stale;
    }));
}
std::optional<std::uint64_t> usage() {
    std::lock_guard lock(mutex);
    return usageBytes;
}
bool clear() {
    std::lock_guard lock(mutex);
    if (!attachedWorld || !root || finishing) return false;
    ++generation;
    ++epochs;
    residents.clear();
    images.clear();
    builtFrom.clear();
    explored.clear();
    dirty.clear();
    indexing = false;
    saves.clear();
    loads.clear();
    removing = root;
    usageBytes = 0;
    wake.notify_one();
    log("cleared the saved map");
    return true;
}
std::optional<ExploredIndex::Bounds> exploredBounds(MapLayer layer) {
    std::lock_guard lock(mutex);
    auto found = explored.find({layer.dimension, layer.band});
    return found == explored.end() ? std::nullopt : found->second.bounds();
}
unsigned epoch() {
    std::lock_guard lock(mutex);
    return epochs;
}
void start() {
    exitListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>(
        [](auto&) { detach(); });
    if (!exitListener) throw std::runtime_error("Could not subscribe world map world changes");
}
void stop() {
    if (exitListener) {
        ll::event::EventBus::getInstance().removeListener(exitListener);
        exitListener.reset();
    }
    detach();
}
}
