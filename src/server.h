#pragma once
#include "models.h"
#include "storage.h"
#include "algorithms.h"
#include "logger.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>

class LocalApiServer {
public:
    LocalApiServer(Storage& storage, Logger& logger)
        : storage_(storage), logger_(logger) {
        data_ = storage_.loadAll();
    }

    void start(const std::string& host = "127.0.0.1", int port = 8080) {
        configureRoutes();

        server_.set_exception_handler([this](const auto&, auto& res, std::exception_ptr ep) {
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                logger_.error(std::string("Unhandled HTTP exception: ") + e.what());
                res.status = 500;
                res.set_content(R"({"status":"error","message":"Internal server error"})", "application/json");
            }
        });

        serverThread_ = std::thread([this, host, port]() {
            logger_.info("Local API server started on " + host + ":" + std::to_string(port));
            server_.listen(host.c_str(), port);
        });
    }

    void stop() {
        server_.stop();
        if (serverThread_.joinable()) serverThread_.join();
        logger_.info("Local API server stopped");
    }

    ~LocalApiServer() {
        stop();
    }

private:
    Storage& storage_;
    Logger& logger_;
    httplib::Server server_;
    std::thread serverThread_;
    std::mutex mtx_;

    std::vector<Sport> data_;
    SortField currentSortField_ = SortField::None;
    bool isSorted_ = false;

    void configureRoutes() {
        using json = nlohmann::json;

        server_.Get("/api/sports", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mtx_);

            int page = 1;
            int limit = 10;

            if (req.has_param("page")) page = std::stoi(req.get_param_value("page"));
            if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));

            if (page < 1) page = 1;
            if (limit < 1) limit = 10;

            int total = static_cast<int>(data_.size());
            int start = (page - 1) * limit;
            int end = std::min(start + limit, total);

            json records = json::array();
            if (start < total) {
                for (int i = start; i < end; ++i) {
                    records.push_back(toJson(data_[i]));
                }
            }

            json response = {
                {"status", "success"},
                {"data", {
                    {"page", page},
                    {"limit", limit},
                    {"total", total},
                    {"records", records}
                }}
            };

            res.set_content(response.dump(2), "application/json");
        });

        server_.Post("/api/sports", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mtx_);
            using json = nlohmann::json;

            json body = json::parse(req.body);

            Sport s;
            s.sport_id = body.at("sport_id").get<int>();
            s.name = body.at("name").get<std::string>();
            s.category = body.at("category").get<std::string>();
            s.olympic_status = body.at("olympic_status").get<bool>();
            s.description = body.at("description").get<std::string>();
            s.governing_body = body.at("governing_body").get<std::string>();
            s.image_path = body.at("image_path").get<std::string>();
            s.medical_contraindications = body.at("medical_contraindications").get<std::string>();
            s.weight = 1;

            validate(s);

	    for (const auto& item : data_) {
    		if (item.sport_id == s.sport_id) {
        	    res.status = 400;
        	    res.set_content(
            		R"({"status":"error","message":"Запись с таким sport_id уже существует"})",
            		"application/json"
        	    );
        	    logger_.warning("Duplicate sport_id rejected: " + std::to_string(s.sport_id));
        	    return;
    		}
	    }

	    data_.push_back(s);
	    storage_.saveAll(data_);

            data_.push_back(s);
            storage_.saveAll(data_);

            isSorted_ = false;
            currentSortField_ = SortField::None;

            json response = {
                {"status", "success"},
                {"message", "Record added"}
            };

            res.set_content(response.dump(2), "application/json");
        });

        server_.Post("/api/sports/sort", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mtx_);
            using json = nlohmann::json;

            std::string fieldStr = "name";
            if (req.has_param("field")) fieldStr = req.get_param_value("field");

            SortField field = sortFieldFromString(fieldStr);
            if (field == SortField::None) {
                res.status = 400;
                res.set_content(R"({"status":"error","message":"Unknown sort field"})", "application/json");
                return;
            }

            if (!data_.empty()) {
                quickSort(data_, 0, static_cast<int>(data_.size()) - 1, field);
            }

            currentSortField_ = field;
            isSorted_ = true;
            storage_.saveAll(data_);

            json response = {
                {"status", "success"},
                {"message", "Sorted"},
                {"field", fieldStr}
            };

            res.set_content(response.dump(2), "application/json");
        });

        server_.Get("/api/sports/search", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mtx_);
            using json = nlohmann::json;

            if (!isSorted_ || currentSortField_ != SortField::Name) {
                res.status = 400;
                res.set_content(
                    R"({"status":"error","message":"Для двоичного поиска данные должны быть отсортированы по name"})",
                    "application/json"
                );
                logger_.warning("Search rejected: data is not sorted by name");
                return;
            }

            if (!req.has_param("name")) {
                res.status = 400;
                res.set_content(R"({"status":"error","message":"Missing 'name' query parameter"})", "application/json");
                return;
            }

            std::string name = req.get_param_value("name");
            int idx = binarySearchVersion2ByName(data_, name);

            if (idx == -1) {
                res.status = 404;
                res.set_content(R"({"status":"error","message":"Record not found"})", "application/json");
                logger_.info("Search failed: " + name);
                return;
            }

            data_[idx].weight += 1; // накапливаем вес по обращениям
            storage_.saveAll(data_);

            json response = {
                {"status", "success"},
                {"record", toJson(data_[idx])}
            };

            res.set_content(response.dump(2), "application/json");
            logger_.info("Search success: " + name);
        });

        server_.Get("/api/sports/tree", [this](const httplib::Request&, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mtx_);
            using json = nlohmann::json;

            std::vector<Sport> copy = data_;
            if (!copy.empty()) {
                quickSort(copy, 0, static_cast<int>(copy.size()) - 1, SortField::Name);
            }

            TreeNode* root = buildTreeByWeightA1(copy);
            json response = {
                {"status", "success"},
                {"tree", treeToJson(root)}
            };

            destroyTree(root);
            res.set_content(response.dump(2), "application/json");
        });
    }

    static nlohmann::json toJson(const Sport& s) {
        return {
            {"sport_id", s.sport_id},
            {"name", s.name},
            {"category", s.category},
            {"olympic_status", s.olympic_status},
            {"description", s.description},
            {"governing_body", s.governing_body},
            {"image_path", s.image_path},
            {"medical_contraindications", s.medical_contraindications},
            {"weight", s.weight}
        };
    }

    static void validate(const Sport& s) {
        if (s.sport_id <= 0) throw std::runtime_error("sport_id должен быть > 0");
        if (s.name.empty()) throw std::runtime_error("name не должен быть пустым");
        if (s.category.empty()) throw std::runtime_error("category не должен быть пустым");
    }
};
