// SPDX-License-Identifier: GPL-2.0-only

#include <rawfiles.h>
#include <versekey.h>

#include <filesystem>
#include <iostream>
#include <string>

int main(const int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: getbiblesword_corpus_writer RAWFILES_PATH\n";
        return 2;
    }

    const std::filesystem::path output(argv[1]);
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) {
        std::cerr << "Unable to create RawFiles corpus directory: " << error.message() << '\n';
        return 1;
    }
    if (sword::RawFiles::createModule(output.c_str()) != 0) {
        std::cerr << "CrossWire SWORD could not initialize the RawFiles corpus module.\n";
        return 1;
    }

    sword::RawFiles module(output.c_str(), "CorpusRawFiles", "Public-domain RawFiles fixture");
    sword::VerseKey key;
    key.setPersist(true);
    key.setText("Gen.1.1");
    module.setKey(key);
    const std::string first = "A public-domain file-backed commentary entry.";
    module.setEntry(first.data(), static_cast<long>(first.size()));
    if (module.popError() != 0) {
        std::cerr << "CrossWire SWORD could not write the first RawFiles entry.\n";
        return 1;
    }

    key.setText("Gen.1.2");
    module.setKey(key);
    const std::string second = "A second file-backed commentary entry.";
    module.setEntry(second.data(), static_cast<long>(second.size()));
    if (module.popError() != 0) {
        std::cerr << "CrossWire SWORD could not write the second RawFiles entry.\n";
        return 1;
    }
    return 0;
}
