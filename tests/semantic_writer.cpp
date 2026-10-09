// SPDX-License-Identifier: GPL-2.0-only

#include <rawld.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(const int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: semantic_writer MODULE_PREFIX FIRST_PAYLOAD SECOND_PAYLOAD\n";
        return 2;
    }
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output.parent_path());
    if (sword::RawLD::createModule(output.c_str()) != 0) {
        return 1;
    }
    sword::RawLD module(output.c_str(), "SemanticFixture", "Public-domain semantic fixture");
    for (int index = 2; index < 4; ++index) {
        std::ifstream stream(argv[index], std::ios::binary);
        if (!stream) {
            return 1;
        }
        const std::string value((std::istreambuf_iterator<char>(stream)), {});
        module.setKey(index == 2 ? "ALPHA" : "BETA");
        module.setEntry(value.data(), static_cast<long>(value.size()));
        if (module.popError() != 0) {
            return 1;
        }
    }
    return 0;
}
