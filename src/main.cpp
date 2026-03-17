#include "models.h"
#include "logger.h"
#include "storage.h"
#include "server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <thread>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>

struct UiState {
    int page = 1;
    int limit = 10;
    int total = 0;

    std::vector<nlohmann::json> records;
    nlohmann::json tree;

    char searchName[256] = "";
    char sortField[64] = "name";

    char sportId[32] = "";
    char name[256] = "";
    char category[256] = "";
    bool olympic = false;
    char description[512] = "";
    char governingBody[256] = "";
    char imagePath[256] = "";
    char contraindications[512] = "";

    std::string status;
};

static bool fetchPage(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);
    auto res = cli.Get(("/api/sports?page=" + std::to_string(ui.page) +
                        "&limit=" + std::to_string(ui.limit)).c_str());
    if (!res) {
        ui.status = "Не удалось подключиться к локальному API";
        return false;
    }

    auto json = nlohmann::json::parse(res->body);
    if (json["status"] != "success") {
        ui.status = "Ошибка ответа API";
        return false;
    }

    ui.total = json["data"]["total"].get<int>();
    ui.records.clear();
    for (auto& rec : json["data"]["records"]) {
        ui.records.push_back(rec);
    }

    ui.status = "Данные обновлены";
    return true;
}

static void doSort(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);
    auto res = cli.Post(("/api/sports/sort?field=" + std::string(ui.sortField)).c_str(), "", "application/json");

    if (!res) {
        ui.status = "Ошибка сортировки: нет ответа от API";
        return;
    }

    ui.status = res->body;
    fetchPage(ui);
}

static void doSearch(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);
    auto res = cli.Get(("/api/sports/search?name=" + std::string(ui.searchName)).c_str());

    if (!res) {
        ui.status = "Ошибка поиска: нет ответа от API";
        return;
    }

    if (res->status != 200) {
        ui.status = res->body;
        return;
    }

    ui.status = res->body;
}

static void buildTree(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);
    auto res = cli.Get("/api/sports/tree");

    if (!res) {
        ui.status = "Ошибка построения дерева";
        return;
    }

    auto json = nlohmann::json::parse(res->body);
    ui.tree = json["tree"];
    ui.status = "Дерево построено";
}

static void addRecord(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);

    nlohmann::json body = {
        {"sport_id", std::atoi(ui.sportId)},
        {"name", std::string(ui.name)},
        {"category", std::string(ui.category)},
        {"olympic_status", ui.olympic},
        {"description", std::string(ui.description)},
        {"governing_body", std::string(ui.governingBody)},
        {"image_path", std::string(ui.imagePath)},
        {"medical_contraindications", std::string(ui.contraindications)}
    };

    auto res = cli.Post("/api/sports", body.dump(), "application/json");

    if (!res) {
        ui.status = "Ошибка добавления записи";
        return;
    }

    ui.status = res->body;
    fetchPage(ui);
}

static void drawTreeText(const nlohmann::json& node, int depth = 0) {
    if (node.is_null()) return;

    std::string prefix(depth * 4, ' ');
    std::string line = prefix + node["key"].get<std::string>() +
                       " (w:" + std::to_string(node["weight"].get<int>()) + ")";
    ImGui::TextUnformatted(line.c_str());

    drawTreeText(node["left"], depth + 1);
    drawTreeText(node["right"], depth + 1);
}

int main() {
    std::filesystem::create_directories("data");
    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("assets/images");

    Logger logger("logs/app.log");
    Storage storage("data/sports_database.dat", "data/sports_database_backup.dat", logger);
    LocalApiServer server(storage, logger);
    server.start();

    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return 1;
    }

    const char* glsl_version = "#version 130";
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Sports Directory", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = false;

    io.Fonts->AddFontFromFileTTF(
	"assets/fonts/DejaVuSans.ttf",
	18.0f,
	&font_cfg,
	io.Fonts->GetGlyphRangesCyrillic()
);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    UiState ui;
    fetchPage(ui);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        ImGui::Begin("Управление");
        ImGui::Text("Страница: %d", ui.page);
        ImGui::Text("Всего записей: %d", ui.total);

        if (ImGui::Button("Назад")) {
            if (ui.page > 1) {
                ui.page--;
                fetchPage(ui);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Вперед")) {
            int maxPage = (ui.total + ui.limit - 1) / ui.limit;
            if (ui.page < maxPage) {
                ui.page++;
                fetchPage(ui);
            }
        }

        ImGui::Separator();

        ImGui::InputText("Поле сортировки", ui.sortField, sizeof(ui.sortField));
        if (ImGui::Button("Сортировка")) {
            doSort(ui);
        }

        ImGui::InputText("Имя для поиска", ui.searchName, sizeof(ui.searchName));
        if (ImGui::Button("Поиск (binary search v2)")) {
            doSearch(ui);
        }

        if (ImGui::Button("Построить дерево A1")) {
            buildTree(ui);
        }

        ImGui::Separator();
        ImGui::TextWrapped("Статус: %s", ui.status.c_str());
        ImGui::End();

        ImGui::Begin("Добавление записи");
        ImGui::InputText("sport_id", ui.sportId, sizeof(ui.sportId));
        ImGui::InputText("name", ui.name, sizeof(ui.name));
        ImGui::InputText("category", ui.category, sizeof(ui.category));
        ImGui::Checkbox("olympic_status", &ui.olympic);
        ImGui::InputTextMultiline("description", ui.description, sizeof(ui.description), ImVec2(-1, 80));
        ImGui::InputText("governing_body", ui.governingBody, sizeof(ui.governingBody));
        ImGui::InputText("image_path", ui.imagePath, sizeof(ui.imagePath));
        ImGui::InputTextMultiline("medical_contraindications", ui.contraindications, sizeof(ui.contraindications), ImVec2(-1, 80));

        if (ImGui::Button("Добавить и сохранить")) {
            addRecord(ui);
        }
        ImGui::End();

        ImGui::Begin("Записи");
        for (const auto& rec : ui.records) {
            ImGui::Separator();
            ImGui::Text("ID: %d", rec["sport_id"].get<int>());
            ImGui::Text("Name: %s", rec["name"].get<std::string>().c_str());
            ImGui::Text("Category: %s", rec["category"].get<std::string>().c_str());
            ImGui::Text("Olympic: %s", rec["olympic_status"].get<bool>() ? "true" : "false");
            ImGui::TextWrapped("Description: %s", rec["description"].get<std::string>().c_str());
            ImGui::Text("Body: %s", rec["governing_body"].get<std::string>().c_str());
            ImGui::Text("Image path: %s", rec["image_path"].get<std::string>().c_str());
            ImGui::TextWrapped("Contraindications: %s", rec["medical_contraindications"].get<std::string>().c_str());

            bool imgExists = std::filesystem::exists(rec["image_path"].get<std::string>());
            ImGui::Text("Image status: %s", imgExists ? "file found" : "placeholder needed");
        }
        ImGui::End();

        ImGui::Begin("Дерево A1");
        if (!ui.tree.is_null()) {
            drawTreeText(ui.tree);
        } else {
            ImGui::TextUnformatted("Пока дерево не построено.");
        }
        ImGui::End();

        ImGui::Begin("График весов");
        if (!ui.records.empty()) {
            static std::vector<double> xs;
            static std::vector<double> ys;
            xs.clear();
            ys.clear();

            for (size_t i = 0; i < ui.records.size(); ++i) {
                xs.push_back(static_cast<double>(i));
                ys.push_back(static_cast<double>(ui.records[i].value("weight", 1)));
            }

            if (ImPlot::BeginPlot("Weights")) {
                ImPlot::PlotBars("weight", xs.data(), ys.data(), static_cast<int>(xs.size()), 0.5);
                ImPlot::EndPlot();
            }
        } else {
            ImGui::TextUnformatted("Нет данных для графика.");
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
