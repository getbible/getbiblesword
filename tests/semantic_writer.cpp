// SPDX-License-Identifier: GPL-2.0-only

#include <rawld.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(const int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: semantic_writer MODULE_PREFIX FIRST_PAYLOAD SECOND_PAYLOAD\n";
        return 2;
    }
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output.parent_path());
    // SWORD 1.9 RawStr::createModule leaks its temporary filename buffer. Its
    // only setup work is creating these two empty files. Avoid that unrelated
    // engine leak without suppressing LeakSanitizer for writer or extractor;
    // every entry below is still serialized by SWORD's official RawLD writer.
    for (const auto* extension : {".dat", ".idx"}) {
        const std::ofstream empty(output.string() + extension, std::ios::binary);
        if (!empty) {
            return 1;
        }
    }
    sword::RawLD module(output.c_str(), "SemanticFixture", "Public-domain semantic fixture");
    for (int index = 2; index < 4; ++index) {
        std::ifstream stream(argv[index], std::ios::binary);
        if (!stream) {
            return 1;
        }
        const auto size = std::filesystem::file_size(argv[index]);
        if (size > 60'000U) {
            std::cerr << "Fixture exceeds the RawLD record limit.\n";
            return 1;
        }
        std::string value(static_cast<std::size_t>(size), '\0');
        stream.read(value.data(), static_cast<std::streamsize>(size));
        if (!stream) {
            return 1;
        }
        module.setKey(index == 2 ? "ALPHA" : "BETA");
        module.setEntry(value.data(), static_cast<long>(value.size()));
        if (module.popError() != 0) {
            return 1;
        }
    }
    return 0;
}
