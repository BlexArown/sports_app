#pragma once
#include "models.h"
#include "logger.h"

#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdint>

class Storage {
public:
    Storage(const std::string& dbPath,
            const std::string& backupPath,
            Logger& logger)
        : dbPath_(dbPath), backupPath_(backupPath), logger_(logger) {
        std::filesystem::create_directories(std::filesystem::path(dbPath_).parent_path());
    }

    std::vector<Sport> loadAll() {
        std::vector<Sport> result;

        if (!std::filesystem::exists(dbPath_)) {
            std::ofstream create(dbPath_, std::ios::binary);
            logger_.warning("DB file not found. Created new empty file.");
            return result;
        }

        std::ifstream in(dbPath_, std::ios::binary);
        if (!in) {
            logger_.error("Failed to open DB file for reading.");
            throw std::runtime_error("Ошибка чтения файла БД");
        }

        while (in.peek() != EOF) {
            Sport s;
            if (!readInt32(in, s.sport_id)) break;
            if (!readString(in, s.name)) break;
            if (!readString(in, s.category)) break;
            if (!readBool(in, s.olympic_status)) break;
            if (!readString(in, s.description)) break;
            if (!readString(in, s.governing_body)) break;
            if (!readString(in, s.image_path)) break;
            if (!readString(in, s.medical_contraindications)) break;
            s.weight = 1;
            result.push_back(s);
        }

        logger_.info("Loaded records: " + std::to_string(result.size()));
        return result;
    }

    void saveAll(const std::vector<Sport>& data) {
        createBackup();

        std::ofstream out(dbPath_, std::ios::binary | std::ios::trunc);
        if (!out) {
            logger_.error("Failed to open DB file for writing.");
            throw std::runtime_error("Ошибка записи файла БД");
        }

        for (const auto& s : data) {
            writeInt32(out, s.sport_id);
            writeString(out, s.name);
            writeString(out, s.category);
            writeBool(out, s.olympic_status);
            writeString(out, s.description);
            writeString(out, s.governing_body);
            writeString(out, s.image_path);
            writeString(out, s.medical_contraindications);
        }

        logger_.info("Saved records: " + std::to_string(data.size()));
    }

private:
    std::string dbPath_;
    std::string backupPath_;
    Logger& logger_;

    void createBackup() {
        if (std::filesystem::exists(dbPath_)) {
            std::filesystem::copy_file(
                dbPath_,
                backupPath_,
                std::filesystem::copy_options::overwrite_existing
            );
            logger_.info("Backup created: " + backupPath_);
        }
    }

    static void writeInt32(std::ofstream& out, int32_t value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    static bool readInt32(std::ifstream& in, int32_t& value) {
        return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
    }

    static void writeBool(std::ofstream& out, bool value) {
        uint8_t b = value ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&b), sizeof(b));
    }

    static bool readBool(std::ifstream& in, bool& value) {
        uint8_t b{};
        if (!in.read(reinterpret_cast<char*>(&b), sizeof(b))) return false;
        value = (b != 0);
        return true;
    }

    static void writeString(std::ofstream& out, const std::string& str) {
        if (str.size() > 65535) {
            throw std::runtime_error("Строка слишком длинная для формата uint16_t");
        }
        uint16_t len = static_cast<uint16_t>(str.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(str.data(), len);
    }

    static bool readString(std::ifstream& in, std::string& str) {
        uint16_t len{};
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        str.resize(len);
        return static_cast<bool>(in.read(str.data(), len));
    }
};
