#include "pch.h"

#include "doctest/doctest.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "game/buffer.h"
#include "game/farload.h"
#include "game/vfs.h"

#include "miniz.h"

namespace test_farload_buffer_detail {
    namespace fs = std::filesystem;

    class Scoped_Test_Directory {
      public:
        Scoped_Test_Directory() {
            const auto unique_id = std::chrono::steady_clock::now().time_since_epoch().count();
            root_path_ = fs::temp_directory_path() / ("orion2re-farload-" + std::to_string(unique_id));
            fs::create_directories(root_path_);
        }

        ~Scoped_Test_Directory() {
            vfs::Shutdown();
            std::error_code error;
            fs::remove_all(root_path_, error);
        }

        [[nodiscard]] const fs::path& root_path() const {
            return root_path_;
        }

        void Write_File(const fs::path& relative_path, const std::string& contents) const {
            const fs::path full_path = root_path_ / relative_path;
            fs::create_directories(full_path.parent_path());
            std::ofstream output(full_path, std::ios::binary);
            DOCTEST_REQUIRE(output.good());
            output.write(contents.data(), (std::streamsize)contents.size());
            DOCTEST_REQUIRE(output.good());
        }

        void Write_Lbx_File(const fs::path& relative_path, const std::vector<std::string>& entries) const {
            const fs::path full_path = root_path_ / relative_path;
            s_lbx_disk_header header{};
            uint32_t offsets[510] = {};
            uint32_t next_offset = sizeof(header) + sizeof(offsets);

            fs::create_directories(full_path.parent_path());
            header.entry_count = (uint16_t)entries.size();
            header.magic = 0xFEAD;
            header.header_data = 0;
            offsets[0] = next_offset;

            for (size_t i = 0; i < entries.size(); ++i) {
                next_offset += (uint32_t)entries[i].size();
                offsets[i + 1] = next_offset;
            }

            std::ofstream output(full_path, std::ios::binary);
            DOCTEST_REQUIRE(output.good());
            output.write((const char*)&header, sizeof(header));
            output.write((const char*)offsets, sizeof(offsets));
            for (const std::string& entry : entries) {
                output.write(entry.data(), (std::streamsize)entry.size());
            }
            DOCTEST_REQUIRE(output.good());
        }

        void Write_Zip_File(const fs::path& relative_path, const std::vector<std::pair<std::string, std::string>>& entries) const {
            const fs::path full_path = root_path_ / relative_path;
            mz_zip_archive archive{};

            fs::create_directories(full_path.parent_path());
            mz_zip_zero_struct(&archive);
            DOCTEST_REQUIRE(mz_zip_writer_init_file(&archive, full_path.string().c_str(), 0) == MZ_TRUE);

            for (const auto& entry : entries) {
                DOCTEST_REQUIRE(mz_zip_writer_add_mem(&archive, entry.first.c_str(), entry.second.data(), entry.second.size(), MZ_BEST_COMPRESSION) == MZ_TRUE);
            }

            DOCTEST_REQUIRE(mz_zip_writer_finalize_archive(&archive) == MZ_TRUE);
            DOCTEST_REQUIRE(mz_zip_writer_end(&archive) == MZ_TRUE);
        }

      private:
        fs::path root_path_;
    };

    struct Scoped_Buffer_Pool {
        std::vector<uint8_t> storage;
        uint8_t* payload;

        explicit Scoped_Buffer_Pool(uint32_t payload_size)
            : storage(sizeof(allocate::s_alloc_header) + payload_size), payload(storage.data() + sizeof(allocate::s_alloc_header)) {
            auto* header = (allocate::s_alloc_header*)storage.data();
            header->current = 0;
            header->size = payload_size;
            header->reserved2 = 0;
        }
    };

    std::string Read_File_Contents(const fs::path& full_path) {
        std::ifstream input(full_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    }
} // namespace test_farload_buffer_detail

DOCTEST_TEST_SUITE("farload buffer vfs") {
    DOCTEST_TEST_CASE("cache invalidation changes resource provenance without moving the arena") {
        test_farload_buffer_detail::Scoped_Test_Directory test_dir;
        test_dir.Write_File("data/TEST.LBX/000", std::string(16, 'B'));
        test_dir.Write_File("data/mods/visual/data/TEST.LBX/000", std::string(31, 'M'));
        DOCTEST_REQUIRE(vfs::Init(test_dir.root_path().string().c_str()) == 1);
        void* pool = buffer::Allocate_Buffer_Space(sizeof(s_file_buffer_entry) * 50 + 1024);
        void* metadata = buffer::file_buffer;
        auto* header = (allocate::s_alloc_header*)pool - 1;
        for (int repeat = 0; repeat < 30; ++repeat) {
            std::string diagnostic;
            auto* transaction = vfs::Begin_Mount_Transaction();
            DOCTEST_REQUIRE(transaction != nullptr);
            if (repeat % 2 != 0) {
                DOCTEST_REQUIRE(vfs::Stage_Package_Mount(transaction, "visual", &diagnostic));
            }
            DOCTEST_REQUIRE(vfs::Commit_Mount_Transaction(transaction));
            buffer::Reset_Buffer_Cache();
            DOCTEST_CHECK(buffer::buffer_space_seg == pool);
            DOCTEST_CHECK(buffer::file_buffer == metadata);
            DOCTEST_CHECK(header->current == 0);
            DOCTEST_CHECK(buffer::file_buffer_count == 0);
            DOCTEST_CHECK(buffer::global_load_count == 0);
            const size_t size = repeat % 2 != 0 ? 31 : 16;
            const char expected = repeat % 2 != 0 ? 'M' : 'B';
            void* loaded = buffer::Buffer_Reload("TEST.LBX", 0, pool);
            DOCTEST_CHECK(std::string((char*)loaded, size) == std::string(size, expected));
            DOCTEST_CHECK(header->current == allocate::Nested_No_Header_Pool_Size((uint32_t)size));
            DOCTEST_CHECK(buffer::Buffer_Reload("test.lbx", 0, pool) == loaded);
            const auto cursor = header->current;
            buffer::Flush_Buffer_Space(nullptr);
            DOCTEST_CHECK(header->current == cursor);
            DOCTEST_CHECK(buffer::file_buffer[0].segments[0].is_loaded == 1);
        }
        buffer::Free_Buffer_Space();
        buffer::Reset_Buffer_Cache();
        DOCTEST_CHECK(buffer::buffer_space_seg == nullptr);
    }

    DOCTEST_TEST_CASE("farload library loads an LBX entry through vfs per-entry override") {
        test_farload_buffer_detail::Scoped_Test_Directory test_dir;
        test_dir.Write_Lbx_File("TEST.LBX", {"raw-zero", "raw-one"});
        test_dir.Write_File(test_farload_buffer_detail::fs::path("data") / "overrides" / "TEST.LBX" / "001", "override-one");

        DOCTEST_REQUIRE(vfs::Init(test_dir.root_path().string().c_str()) == 1);

        char pool[32] = {};
        void* loaded = farload::Farload_Library("TEST.LBX", 1, pool, 0);
        DOCTEST_REQUIRE(loaded != nullptr);
        DOCTEST_CHECK(std::string((const char*)loaded, 12) == "override-one");
        std::free(loaded);
    }

    DOCTEST_TEST_CASE("farload library data reads structured records from whole-LBX zip replacement") {
        test_farload_buffer_detail::Scoped_Test_Directory test_dir;

        std::string structured_entry;
        uint16_t total_count = 3;
        uint16_t element_size = 2;
        structured_entry.append((const char*)&total_count, sizeof(total_count));
        structured_entry.append((const char*)&element_size, sizeof(element_size));
        structured_entry.push_back('A');
        structured_entry.push_back('1');
        structured_entry.push_back('B');
        structured_entry.push_back('2');
        structured_entry.push_back('C');
        structured_entry.push_back('3');

        test_dir.Write_Lbx_File("ZIPSRC.LBX", {structured_entry});
        std::string zip_lbx = test_farload_buffer_detail::Read_File_Contents(test_dir.root_path() / "ZIPSRC.LBX");
        test_dir.Write_Zip_File(test_farload_buffer_detail::fs::path("data") / "base.zip", {
                                                                                               {"TESTDATA.LBX", zip_lbx},
                                                                                           });

        DOCTEST_REQUIRE(vfs::Init(test_dir.root_path().string().c_str()) == 1);

        char output[4] = {};
        void* loaded = farload::Farload_Library_Data("TESTDATA.LBX", 0, output, 3, 1, 2, 2);
        DOCTEST_REQUIRE(loaded == output);
        DOCTEST_CHECK(std::string(output, 4) == "B2C3");
        DOCTEST_CHECK(farload::Get_Farload_Data_Record_Count("TESTDATA.LBX", 0) == 3);
    }

    DOCTEST_TEST_CASE("buffer farload library uses vfs lbx entry resolution") {
        test_farload_buffer_detail::Scoped_Test_Directory test_dir;
        test_dir.Write_Lbx_File("BUFFER.LBX", {"raw-entry"});

        test_dir.Write_Lbx_File("BUFFERZIP.LBX", {"zip-entry"});
        std::string zip_lbx = test_farload_buffer_detail::Read_File_Contents(test_dir.root_path() / "BUFFERZIP.LBX");
        test_dir.Write_Zip_File(test_farload_buffer_detail::fs::path("data") / "base.zip", {
                                                                                               {"BUFFER.LBX", zip_lbx},
                                                                                           });

        DOCTEST_REQUIRE(vfs::Init(test_dir.root_path().string().c_str()) == 1);

        test_farload_buffer_detail::Scoped_Buffer_Pool pool(64);
        void* loaded = buffer::Farload_Buffer_Library("BUFFER.LBX", 0, pool.payload);
        DOCTEST_REQUIRE(loaded != nullptr);
        DOCTEST_CHECK(std::string((const char*)loaded, 9) == "zip-entry");
    }
}