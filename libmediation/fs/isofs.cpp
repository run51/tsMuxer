#include "isofs.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>

#include "file.h"

namespace
{
constexpr uint32_t SECTOR = 2048;
constexpr int MAX_DEPTH = 32;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint64_t rd64(const uint8_t* p) { return rd32(p) | static_cast<uint64_t>(rd32(p + 4)) << 32; }

// Part of a file's data: where it lies in the image, or a run of zeros the disc does not record.
struct Extent
{
    uint64_t offset;
    uint64_t length;
    bool zero;
};

struct Entry
{
    bool dir = false;
    uint64_t size = 0;
    std::vector<Extent> extents;
    std::string inlineData;  // a small file kept inside its own file entry
    std::vector<std::string> children;  // the names as the disc writes them
};

// One partition map of the logical volume. A Blu-ray has two: the physical partition, and a
// metadata partition whose blocks are the data of a metadata file stored in the physical one.
struct PartMap
{
    bool metadata = false;
    uint16_t partNum = 0;
    uint32_t metaFileLbn = 0;
    uint32_t metaMirrorLbn = 0;
    uint64_t start = 0;  // physical partition: its first sector
    std::vector<Extent> metaExtents;  // metadata partition: where its blocks lie in the image
};

std::string upperAscii(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
}

void appendUtf8(std::string& out, uint32_t c)
{
    if (c < 0x80)
        out += static_cast<char>(c);
    else if (c < 0x800)
    {
        out += static_cast<char>(0xC0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xE0 | c >> 12);
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// A UDF d-string: a compression id, 8 (one byte per character) or 16 (UTF-16 big endian).
std::string decodeName(const uint8_t* p, size_t len)
{
    std::string out;
    if (len == 0)
        return out;
    if (p[0] == 8)
        for (size_t i = 1; i < len; ++i) appendUtf8(out, p[i]);
    else if (p[0] == 16)
        for (size_t i = 1; i + 1 < len; i += 2) appendUtf8(out, static_cast<uint32_t>(p[i] << 8 | p[i + 1]));
    return out;
}

class Volume
{
   public:
    bool load(const std::string& imagePath)
    {
        if (!m_img.open(imagePath.c_str(), File::ofRead | File::ofOpenExisting))
            return false;
        const bool ok = loadVolume();
        m_img.close();
        return ok;
    }

    std::map<std::string, Entry> entries;  // key: the path on the disc in upper case, "" for the root

   private:
    File m_img;
    std::vector<PartMap> m_maps;
    std::map<uint16_t, uint64_t> m_partStart;

    bool readAt(uint64_t offset, uint8_t* buf, uint32_t len)
    {
        if (m_img.seek(static_cast<int64_t>(offset)) != static_cast<int64_t>(offset))
            return false;
        uint32_t done = 0;
        while (done < len)
        {
            const int n = m_img.read(buf + done, len - done);
            if (n <= 0)
                return false;
            done += n;
        }
        return true;
    }

    // Where block lbn of partition map partRef lies in the image.
    bool toImageOffset(uint16_t partRef, uint32_t lbn, uint64_t& offset) const
    {
        if (partRef >= m_maps.size())
            return false;
        const PartMap& map = m_maps[partRef];
        if (!map.metadata)
        {
            offset = (map.start + lbn) * SECTOR;
            return true;
        }
        uint64_t pos = static_cast<uint64_t>(lbn) * SECTOR;
        for (const Extent& e : map.metaExtents)
        {
            if (pos < e.length)
            {
                if (e.zero)
                    return false;
                offset = e.offset + pos;
                return true;
            }
            pos -= e.length;
        }
        return false;
    }

    bool readBlock(uint16_t partRef, uint32_t lbn, uint8_t* buf)
    {
        uint64_t offset;
        return toImageOffset(partRef, lbn, offset) && readAt(offset, buf, SECTOR);
    }

    // Adds blocks [lbn, lbn + len) of a partition to a file's extents. A metadata partition is
    // not contiguous in the image, so it is mapped block by block and joined back where it is.
    bool addExtent(uint16_t partRef, uint32_t lbn, uint32_t len, bool recorded, std::vector<Extent>& out) const
    {
        if (!recorded)
        {
            out.push_back({0, len, true});
            return true;
        }
        if (partRef < m_maps.size() && !m_maps[partRef].metadata)
        {
            uint64_t offset;
            if (!toImageOffset(partRef, lbn, offset))
                return false;
            out.push_back({offset, len, false});
            return true;
        }
        for (uint64_t done = 0; done < len; done += SECTOR)
        {
            uint64_t offset;
            if (!toImageOffset(partRef, lbn + static_cast<uint32_t>(done / SECTOR), offset))
                return false;
            const uint64_t part = std::min<uint64_t>(SECTOR, len - done);
            if (!out.empty() && !out.back().zero && out.back().offset + out.back().length == offset)
                out.back().length += part;
            else
                out.push_back({offset, part, false});
        }
        return true;
    }

    // A file entry (261) or extended file entry (266) and the allocation descriptors after it.
    bool readFileEntry(uint16_t partRef, uint32_t lbn, Entry& entry)
    {
        uint8_t buf[SECTOR];
        if (!readBlock(partRef, lbn, buf))
            return false;
        const uint16_t tag = rd16(buf);
        if (tag != 261 && tag != 266)
            return false;
        const uint8_t fileType = buf[16 + 11];
        const int adType = rd16(buf + 16 + 18) & 7;
        entry.dir = fileType == 4;
        entry.size = rd64(buf + 56);
        const uint32_t base = tag == 261 ? 176 : 216;
        const uint32_t lEA = rd32(buf + (tag == 261 ? 168 : 208));
        uint32_t lAD = rd32(buf + (tag == 261 ? 172 : 212));
        if (base + static_cast<uint64_t>(lEA) + lAD > SECTOR)
            return false;
        if (adType == 3)
        {
            if (entry.size > lAD)
                return false;
            entry.inlineData.assign(reinterpret_cast<const char*>(buf + base + lEA), static_cast<size_t>(entry.size));
            return true;
        }
        if (adType != 0 && adType != 1)
            return false;
        const uint32_t adSize = adType == 0 ? 8 : 16;

        // The descriptors can continue in an allocation extent descriptor (258) elsewhere.
        uint8_t ext[SECTOR];
        const uint8_t* ad = buf + base + lEA;
        for (int hops = 0; hops < 1024;)
        {
            bool next = false;
            for (uint32_t p = 0; p + adSize <= lAD; p += adSize)
            {
                const uint32_t raw = rd32(ad + p);
                const uint32_t len = raw & 0x3FFFFFFF;
                const uint32_t type = raw >> 30;
                if (len == 0)
                    break;
                const uint32_t pos = rd32(ad + p + 4);
                const uint16_t ref = adType == 0 ? partRef : rd16(ad + p + 8);
                if (type == 3)
                {
                    if (!readBlock(ref, pos, ext) || rd16(ext) != 258)
                        return false;
                    lAD = std::min<uint32_t>(rd32(ext + 20), SECTOR - 24);
                    ad = ext + 24;
                    next = true;
                    ++hops;
                    break;
                }
                if (!addExtent(ref, pos, len, type == 0, entry.extents))
                    return false;
            }
            if (!next)
                return true;
        }
        return false;
    }

    bool readData(const Entry& e, std::vector<uint8_t>& out)
    {
        if (!e.inlineData.empty() || e.extents.empty())
        {
            out.assign(e.inlineData.begin(), e.inlineData.end());
            return true;
        }
        if (e.size > 64 * 1024 * 1024)
            return false;
        out.assign(static_cast<size_t>(e.size), 0);
        uint64_t pos = 0;
        for (const Extent& x : e.extents)
        {
            if (pos >= e.size)
                break;
            const uint64_t len = std::min(x.length, e.size - pos);
            if (!x.zero && !readAt(x.offset, out.data() + pos, static_cast<uint32_t>(len)))
                return false;
            pos += len;
        }
        return true;
    }

    void walk(const std::string& key, int depth)
    {
        if (depth > MAX_DEPTH)
            return;
        std::vector<uint8_t> data;
        if (!readData(entries[key], data))
            return;
        std::vector<std::string> subDirs;
        for (size_t off = 0; off + 38 <= data.size();)
        {
            const uint8_t* fid = data.data() + off;
            if (rd16(fid) != 257)
                break;
            const uint8_t characteristics = fid[18];
            const uint8_t lFI = fid[19];
            const uint32_t lbn = rd32(fid + 24);
            const uint16_t ref = rd16(fid + 28);
            const uint16_t lIU = rd16(fid + 36);
            const size_t step = (38 + lIU + lFI + 3) & ~static_cast<size_t>(3);
            if (off + 38 + lIU + lFI > data.size())
                break;
            const std::string name = decodeName(fid + 38 + lIU, lFI);
            off += step;
            if ((characteristics & 0x0C) || name.empty())  // the parent, or a deleted entry
                continue;
            Entry child;
            if (!readFileEntry(ref, lbn, child))
                continue;
            const std::string childKey = key.empty() ? upperAscii(name) : key + "/" + upperAscii(name);
            entries[key].children.push_back(name);
            if (child.dir)
                subDirs.push_back(childKey);
            entries[childKey] = std::move(child);
        }
        for (const std::string& sub : subDirs) walk(sub, depth + 1);
    }

    bool loadVolume()
    {
        uint8_t buf[SECTOR];
        // The anchor volume descriptor pointer, always at sector 256.
        if (!readAt(256ull * SECTOR, buf, SECTOR) || rd16(buf) != 2)
            return false;
        const uint32_t vdsLen = rd32(buf + 16);
        const uint32_t vdsLoc = rd32(buf + 20);

        bool haveLvd = false;
        uint32_t fsdLbn = 0;
        uint16_t fsdRef = 0;
        for (uint32_t i = 0; i < std::min<uint32_t>(vdsLen / SECTOR, 64); ++i)
        {
            if (!readAt(static_cast<uint64_t>(vdsLoc + i) * SECTOR, buf, SECTOR))
                return false;
            const uint16_t tag = rd16(buf);
            if (tag == 8)  // terminating descriptor
                break;
            if (tag == 5)  // partition descriptor
                m_partStart[rd16(buf + 22)] = rd32(buf + 188);
            else if (tag == 6 && !haveLvd)  // logical volume descriptor
            {
                if (rd32(buf + 212) != SECTOR)
                    return false;
                haveLvd = true;
                fsdLbn = rd32(buf + 252);
                fsdRef = rd16(buf + 256);
                const uint32_t mapCount = rd32(buf + 268);
                uint32_t p = 440;
                for (uint32_t m = 0; m < mapCount && p + 2 <= SECTOR; ++m)
                {
                    const uint8_t type = buf[p];
                    const uint8_t len = buf[p + 1];
                    if (len == 0 || p + len > SECTOR)
                        return false;
                    PartMap map;
                    if (type == 1)
                        map.partNum = rd16(buf + p + 4);
                    else if (type == 2 && std::memcmp(buf + p + 5, "*UDF Metadata Partition", 23) == 0)
                    {
                        map.metadata = true;
                        map.partNum = rd16(buf + p + 38);
                        map.metaFileLbn = rd32(buf + p + 40);
                        map.metaMirrorLbn = rd32(buf + p + 44);
                    }
                    else
                        return false;  // a virtual or sparable partition: not a disc image
                    m_maps.push_back(map);
                    p += len;
                }
            }
        }
        if (!haveLvd || m_maps.empty())
            return false;
        for (PartMap& map : m_maps)
        {
            const auto it = m_partStart.find(map.partNum);
            if (it == m_partStart.end())
                return false;
            map.start = it->second;
        }
        // The metadata file is a file of the physical partition it names. Its mirror is the copy.
        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            if (!m_maps[i].metadata)
                continue;
            uint16_t physRef = 0;
            while (physRef < m_maps.size() &&
                   (m_maps[physRef].metadata || m_maps[physRef].partNum != m_maps[i].partNum))
                ++physRef;
            if (physRef == m_maps.size())
                return false;
            Entry metaFile;
            if (!readFileEntry(physRef, m_maps[i].metaFileLbn, metaFile) &&
                !readFileEntry(physRef, m_maps[i].metaMirrorLbn, metaFile))
                return false;
            m_maps[i].metaExtents = metaFile.extents;
        }
        // The file set descriptor, and from it the root folder.
        if (!readBlock(fsdRef, fsdLbn, buf) || rd16(buf) != 256)
            return false;
        Entry root;
        if (!readFileEntry(rd16(buf + 408), rd32(buf + 404), root) || !root.dir)
            return false;
        entries[""] = std::move(root);
        walk("", 0);
        return true;
    }
};

bool isRegularFile(const std::string& utf8Path)
{
    std::error_code ec;
    const std::filesystem::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    return std::filesystem::is_regular_file(p, ec);
}

bool endsWithIso(const std::string& path, size_t end)
{
    if (end < 5)
        return false;
    return upperAscii(path.substr(end - 4, 4)) == ".ISO";
}

// "movie.iso/BDMV/../BDMV/STREAM//00001.m2ts" -> image "movie.iso", key "BDMV/STREAM/00001.M2TS".
bool splitPath(const std::string& path, std::string& image, std::string& key)
{
    for (size_t i = 5; i < path.size(); ++i)
    {
        if ((path[i] != '/' && path[i] != '\\') || !endsWithIso(path, i))
            continue;
        if (!isRegularFile(path.substr(0, i)))
            continue;
        image = path.substr(0, i);
        std::vector<std::string> parts;
        size_t from = i + 1;
        while (from <= path.size())
        {
            size_t to = path.find_first_of("/\\", from);
            if (to == std::string::npos)
                to = path.size();
            const std::string part = path.substr(from, to - from);
            if (part == "..")
            {
                if (!parts.empty())
                    parts.pop_back();
            }
            else if (!part.empty() && part != ".")
                parts.push_back(upperAscii(part));
            from = to + 1;
        }
        key.clear();
        for (const std::string& part : parts) key += (key.empty() ? "" : "/") + part;
        return true;
    }
    return false;
}

std::mutex g_cacheMutex;
std::map<std::string, std::pair<int64_t, std::shared_ptr<const Volume>>> g_cache;

// The parsed image, read once and kept while its size stays the same.
std::shared_ptr<const Volume> volumeOf(const std::string& image)
{
    std::error_code ec;
    const auto sz = static_cast<int64_t>(
        std::filesystem::file_size(std::filesystem::path(std::u8string(image.begin(), image.end())), ec));
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    const auto it = g_cache.find(image);
    if (it != g_cache.end() && it->second.first == sz)
        return it->second.second;
    auto vol = std::make_shared<Volume>();
    if (!vol->load(image))
        vol.reset();
    g_cache[image] = {sz, vol};
    return vol;
}

const Entry* lookup(const std::string& path, std::shared_ptr<const Volume>& vol, std::string* image = nullptr)
{
    std::string img, key;
    if (!splitPath(path, img, key))
        return nullptr;
    vol = volumeOf(img);
    if (!vol)
        return nullptr;
    const auto it = vol->entries.find(key);
    if (image)
        *image = img;
    return it == vol->entries.end() ? nullptr : &it->second;
}

struct Handle
{
    std::shared_ptr<const Volume> vol;
    const Entry* entry = nullptr;
    File img;
    int64_t pos = 0;
};

bool wildcardMatch(const char* mask, const char* name)
{
    if (*mask == '\0')
        return *name == '\0';
    if (*mask == '*')
        return wildcardMatch(mask + 1, name) || (*name && wildcardMatch(mask, name + 1));
    if (*name == '\0')
        return false;
    const auto up = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; };
    return (*mask == '?' || up(*mask) == up(*name)) && wildcardMatch(mask + 1, name + 1);
}

void listFiles(const Volume& vol, const std::string& key, const std::string& dir, const std::string& mask,
               std::vector<std::string>* fileList, bool savePaths, bool recursive, int depth)
{
    const auto it = vol.entries.find(key);
    if (it == vol.entries.end() || depth > MAX_DEPTH)
        return;
    const char sep = dir.find('\\') != std::string::npos ? '\\' : '/';
    for (const std::string& name : it->second.children)
    {
        const std::string childKey = key.empty() ? upperAscii(name) : key + "/" + upperAscii(name);
        const auto child = vol.entries.find(childKey);
        if (child == vol.entries.end())
            continue;
        if (child->second.dir)
        {
            if (recursive)
                listFiles(vol, childKey, dir + name + sep, mask, fileList, savePaths, recursive, depth + 1);
        }
        else if (wildcardMatch(mask.c_str(), name.c_str()))
            fileList->push_back(savePaths || recursive ? dir + name : name);
    }
}
}  // namespace

namespace isofs
{
bool isInside(const std::string& path)
{
    std::string image, key;
    return splitPath(path, image, key);
}

void* open(const std::string& path)
{
    std::shared_ptr<const Volume> vol;
    std::string image;
    const Entry* e = lookup(path, vol, &image);
    if (!e || e->dir)
        return nullptr;
    auto h = std::make_unique<Handle>();
    h->vol = vol;
    h->entry = e;
    if (!e->extents.empty() && !h->img.open(image.c_str(), File::ofRead | File::ofOpenExisting))
        return nullptr;
    return h.release();
}

void close(void* handle) { delete static_cast<Handle*>(handle); }

int read(void* handle, void* buffer, uint32_t count)
{
    auto* h = static_cast<Handle*>(handle);
    const Entry& e = *h->entry;
    if (h->pos < 0)
        return -1;
    const auto size = static_cast<int64_t>(e.size);
    if (h->pos >= size || count == 0)
        return 0;
    count = static_cast<uint32_t>(std::min<int64_t>(count, size - h->pos));
    auto* out = static_cast<uint8_t*>(buffer);
    if (e.extents.empty())
    {
        std::memcpy(out, e.inlineData.data() + h->pos, count);
        h->pos += count;
        return static_cast<int>(count);
    }
    uint32_t done = 0;
    int64_t extStart = 0;
    for (const Extent& x : e.extents)
    {
        if (done == count)
            break;
        const auto extEnd = extStart + static_cast<int64_t>(x.length);
        if (h->pos < extEnd)
        {
            const int64_t inExt = h->pos - extStart;
            const auto part = static_cast<uint32_t>(std::min<int64_t>(count - done, extEnd - h->pos));
            if (x.zero)
                std::memset(out + done, 0, part);
            else
            {
                const auto at = static_cast<int64_t>(x.offset) + inExt;
                if (h->img.seek(at) != at)
                    return done > 0 ? static_cast<int>(done) : -1;
                uint32_t got = 0;
                while (got < part)
                {
                    const int n = h->img.read(out + done + got, part - got);
                    if (n <= 0)
                        return done + got > 0 ? static_cast<int>(done + got) : -1;
                    got += n;
                }
            }
            done += part;
            h->pos += part;
        }
        extStart = extEnd;
    }
    return static_cast<int>(done);
}

int64_t size(void* handle) { return static_cast<int64_t>(static_cast<Handle*>(handle)->entry->size); }

int64_t seek(void* handle, int64_t offset, int whence)
{
    auto* h = static_cast<Handle*>(handle);
    const int64_t base = whence == 1 ? h->pos : whence == 2 ? static_cast<int64_t>(h->entry->size) : 0;
    if (base + offset < 0)
        return -1;
    h->pos = base + offset;
    return h->pos;
}

bool exists(const std::string& path)
{
    std::shared_ptr<const Volume> vol;
    return lookup(path, vol) != nullptr;
}

int64_t fileSize(const std::string& path)
{
    std::shared_ptr<const Volume> vol;
    const Entry* e = lookup(path, vol);
    return e && !e->dir ? static_cast<int64_t>(e->size) : -1;
}

bool findFiles(const std::string& dir, const std::string& mask, std::vector<std::string>* fileList, bool savePaths,
               bool recursive)
{
    std::shared_ptr<const Volume> vol;
    std::string image, key;
    if (!splitPath(dir, image, key) || !(vol = volumeOf(image)))
        return false;
    const auto it = vol->entries.find(key);
    if (it == vol->entries.end() || !it->second.dir)
        return false;
    std::string prefix = dir;
    if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\')
        prefix += dir.find('\\') != std::string::npos ? '\\' : '/';
    listFiles(*vol, key, prefix, mask, fileList, savePaths, recursive, 0);
    return true;
}
}  // namespace isofs
