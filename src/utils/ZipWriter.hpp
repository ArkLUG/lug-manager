#pragma once
#include <zlib.h>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal ZIP writer: "stored" (uncompressed) entries, which suits photos
// (already compressed) and keeps memory flat - files are streamed from disk.
// Classic ZIP32: refuses archives past 4 GB / 65535 entries.
class ZipWriter {
public:
    explicit ZipWriter(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc) {
        if (!out_) throw std::runtime_error("can't create " + path);
    }

    // Adds the file at `src` under `name` (use '/' separators).
    void add_file(const std::string& name, const std::string& src) {
        std::ifstream in(src, std::ios::binary);
        if (!in) throw std::runtime_error("can't read " + src);
        uLong crc = crc32(0L, Z_NULL, 0);
        uint64_t size = 0;
        std::vector<char> buf(1 << 16);
        while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {
            crc = crc32(crc, reinterpret_cast<const Bytef*>(buf.data()), static_cast<uInt>(in.gcount()));
            size += static_cast<uint64_t>(in.gcount());
        }
        uint64_t offset = static_cast<uint64_t>(out_.tellp());
        if (size >= 0xFFFFFFFFull || offset + size + 30 + name.size() >= 0xFFFFFFFFull || entries_.size() >= 0xFFFF)
            throw std::runtime_error("archive too large for ZIP32");
        Entry e{name, static_cast<uint32_t>(crc), static_cast<uint32_t>(size), static_cast<uint32_t>(offset)};
        local_header(e);
        in.clear();
        in.seekg(0);
        while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0)
            out_.write(buf.data(), in.gcount());
        entries_.push_back(std::move(e));
    }

    void add_bytes(const std::string& name, const std::string& data) {
        uint64_t offset = static_cast<uint64_t>(out_.tellp());
        Entry e{name, static_cast<uint32_t>(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.data()),
                                                  static_cast<uInt>(data.size()))),
                static_cast<uint32_t>(data.size()), static_cast<uint32_t>(offset)};
        local_header(e);
        out_.write(data.data(), static_cast<std::streamsize>(data.size()));
        entries_.push_back(std::move(e));
    }

    void finish() {
        uint32_t cd_start = static_cast<uint32_t>(out_.tellp());
        for (const auto& e : entries_) {
            u32(0x02014b50); u16(20); u16(20); u16(0x0800); u16(0);   // central header, UTF-8 names, stored
            u16(0); u16(0x21);                                        // time 00:00, date 1980-01-01
            u32(e.crc); u32(e.size); u32(e.size);
            u16(static_cast<uint16_t>(e.name.size())); u16(0); u16(0); u16(0); u16(0); u32(0);
            u32(e.offset);
            out_.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));
        }
        uint32_t cd_size = static_cast<uint32_t>(out_.tellp()) - cd_start;
        u32(0x06054b50); u16(0); u16(0);
        u16(static_cast<uint16_t>(entries_.size())); u16(static_cast<uint16_t>(entries_.size()));
        u32(cd_size); u32(cd_start); u16(0);
        out_.flush();
        if (!out_) throw std::runtime_error("write failed");
    }

private:
    struct Entry { std::string name; uint32_t crc, size, offset; };

    void local_header(const Entry& e) {
        u32(0x04034b50); u16(20); u16(0x0800); u16(0); u16(0); u16(0x21);
        u32(e.crc); u32(e.size); u32(e.size);
        u16(static_cast<uint16_t>(e.name.size())); u16(0);
        out_.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));
    }
    void u16(uint16_t v) { char b[2] = {char(v & 0xFF), char(v >> 8)}; out_.write(b, 2); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>(v >> 16)); }

    std::ofstream out_;
    std::vector<Entry> entries_;
};
