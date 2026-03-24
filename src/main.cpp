#include "models.h"
#include "logger.h"
#include "storage.h"
#include "server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "portable-file-dialogs.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <algorithm>

struct TextureData {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

struct TreeTabState {
    nlohmann::json tree;
    char searchValue[256] = "";
    std::string status;
    nlohmann::json foundRecord;
    bool hasFoundRecord = false;
};

struct UiState {
    int page = 1;
    int limit = 10;
    int total = 0;

    std::vector<nlohmann::json> records;

    char searchName[256] = "";
    char sortField[64] = "name";

    char sportId[32] = "";
    char name[256] = "";
    char category[256] = "";
    bool olympic = false;
    char description[512] = "";
    char governingBody[256] = "";
    char imagePath[512] = "";
    char contraindications[512] = "";

    std::string selectedImageSourcePath;
    std::string status;

    nlohmann::json binarySearchRecord;
    bool hasBinarySearchRecord = false;

    TreeTabState treeTabs[8];
};

static std::unordered_map<std::string, TextureData> g_textureCache;

static const std::array<SortField, 8> g_treeFields = {
    SortField::Id,
    SortField::Name,
    SortField::Category,
    SortField::OlympicStatus,
    SortField::Description,
    SortField::GoverningBody,
    SortField::ImagePath,
    SortField::MedicalContraindications
};

static const std::array<const char*, 8> g_treeLabels = {
    "sport_id",
    "name",
    "category",
    "olympic_status",
    "description",
    "governing_body",
    "image_path",
    "medical_contraindications"
};

static int fieldToIndex(SortField field) {
    switch (field) {
        case SortField::Id: return 0;
        case SortField::Name: return 1;
        case SortField::Category: return 2;
        case SortField::OlympicStatus: return 3;
        case SortField::Description: return 4;
        case SortField::GoverningBody: return 5;
        case SortField::ImagePath: return 6;
        case SortField::MedicalContraindications: return 7;
        default: return -1;
    }
}

static std::string urlEncode(const std::string& s) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;

    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << int(c);
        }
    }

    return encoded.str();
}

static bool loadTextureFromFile(const std::string& path, TextureData& outTexture) {
    int width = 0;
    int height = 0;
    int channels = 0;

    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        return false;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 data);

    stbi_image_free(data);

    outTexture.id = texture;
    outTexture.width = width;
    outTexture.height = height;
    return true;
}

static TextureData* getTexture(const std::string& path) {
    if (path.empty()) return nullptr;
    if (!std::filesystem::exists(path)) return nullptr;

    auto it = g_textureCache.find(path);
    if (it != g_textureCache.end()) {
        return &it->second;
    }

    TextureData tex;
    if (!loadTextureFromFile(path, tex)) {
        return nullptr;
    }

    g_textureCache[path] = tex;
    return &g_textureCache[path];
}

static void destroyAllTextures() {
    for (auto& item : g_textureCache) {
        if (item.second.id != 0) {
            glDeleteTextures(1, &item.second.id);
        }
    }
    g_textureCache.clear();
}

static void drawImagePreview(const std::string& path, float maxWidth = 220.0f, float maxHeight = 160.0f) {
    if (path.empty()) {
        ImGui::TextUnformatted("Изображение не указано");
        return;
    }

    if (!std::filesystem::exists(path)) {
        ImGui::TextWrapped("Файл изображения не найден: %s", path.c_str());
        ImGui::TextUnformatted("Показывается заглушка");
        return;
    }

    TextureData* tex = getTexture(path);
    if (!tex) {
        ImGui::TextWrapped("Не удалось загрузить изображение: %s", path.c_str());
        return;
    }

    float w = static_cast<float>(tex->width);
    float h = static_cast<float>(tex->height);

    float scale = std::min(maxWidth / w, maxHeight / h);
    if (scale > 1.0f) scale = 1.0f;

    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(tex->id)), ImVec2(w * scale, h * scale));
}

static void clearAddForm(UiState& ui) {
    std::memset(ui.sportId, 0, sizeof(ui.sportId));
    std::memset(ui.name, 0, sizeof(ui.name));
    std::memset(ui.category, 0, sizeof(ui.category));
    ui.olympic = false;
    std::memset(ui.description, 0, sizeof(ui.description));
    std::memset(ui.governingBody, 0, sizeof(ui.governingBody));
    std::memset(ui.imagePath, 0, sizeof(ui.imagePath));
    std::memset(ui.contraindications, 0, sizeof(ui.contraindications));
    ui.selectedImageSourcePath.clear();
}

static std::string copyImageToAssets(const std::string& sourcePath, int sportId) {
    namespace fs = std::filesystem;

    if (sourcePath.empty()) return "";
    if (!fs::exists(sourcePath)) return "";

    fs::create_directories("assets/images");

    fs::path src(sourcePath);
    std::string ext = src.extension().string();
    if (ext.empty()) ext = ".png";

    std::string targetName = "sport_" + std::to_string(sportId) + ext;
    fs::path dst = fs::path("assets/images") / targetName;

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    return dst.string();
}

static void chooseImage(UiState& ui) {
    auto selection = pfd::open_file(
        "Выбор изображения",
        ".",
        { "Image Files", "*.png *.jpg *.jpeg *.bmp *.tga" },
        pfd::opt::none
    ).result();

    if (!selection.empty()) {
        ui.selectedImageSourcePath = selection[0];
        std::snprintf(ui.imagePath, sizeof(ui.imagePath), "%s", ui.selectedImageSourcePath.c_str());
        ui.status = "Файл изображения выбран";
    }
}

static bool fetchPage(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);

    auto res = cli.Get(("/api/sports?page=" + std::to_string(ui.page) +
                        "&limit=" + std::to_string(ui.limit)).c_str());

    if (!res) {
        ui.status = "Не удалось подключиться к локальному API";
        return false;
    }

    if (res->status != 200) {
        ui.status = res->body;
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

    int maxPage = std::max(1, (ui.total + ui.limit - 1) / ui.limit);
    if (ui.page > maxPage) {
        ui.page = maxPage;
    }

    ui.status = "Данные обновлены";
    return true;
}

static void doSort(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);

    auto res = cli.Post(
        ("/api/sports/sort?field=" + std::string(ui.sortField)).c_str(),
        "",
        "application/json"
    );

    if (!res) {
        ui.status = "Ошибка сортировки: нет ответа от API";
        return;
    }

    ui.status = res->body;
    fetchPage(ui);
}

static void doBinarySearch(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);

    std::string encodedName = urlEncode(std::string(ui.searchName));
    auto res = cli.Get(("/api/sports/search?name=" + encodedName).c_str());

    if (!res) {
        ui.status = "Ошибка поиска: нет ответа от API";
        ui.hasBinarySearchRecord = false;
        return;
    }

    if (res->status != 200) {
        ui.status = res->body;
        ui.hasBinarySearchRecord = false;
        return;
    }

    auto json = nlohmann::json::parse(res->body);
    ui.binarySearchRecord = json["record"];
    ui.hasBinarySearchRecord = true;
    ui.status = "Запись найдена бинарным поиском";
}

static void buildTreeForField(UiState& ui, SortField field) {
    int idx = fieldToIndex(field);
    if (idx < 0) return;

    httplib::Client cli("127.0.0.1", 8080);
    std::string fieldStr = sortFieldToString(field);

    auto res = cli.Get(("/api/sports/tree?field=" + fieldStr).c_str());

    if (!res) {
        ui.treeTabs[idx].status = "Ошибка построения дерева";
        return;
    }

    if (res->status != 200) {
        ui.treeTabs[idx].status = res->body;
        return;
    }

    auto json = nlohmann::json::parse(res->body);
    ui.treeTabs[idx].tree = json["tree"];
    ui.treeTabs[idx].status = "Дерево построено";
}

static void searchInTree(UiState& ui, SortField field) {
    int idx = fieldToIndex(field);
    if (idx < 0) return;

    httplib::Client cli("127.0.0.1", 8080);
    std::string fieldStr = sortFieldToString(field);
    std::string value = ui.treeTabs[idx].searchValue;
    std::string encodedValue = urlEncode(value);

    auto res = cli.Get(("/api/sports/tree/search?field=" + fieldStr + "&value=" + encodedValue).c_str());

    if (!res) {
        ui.treeTabs[idx].status = "Ошибка поиска в дереве";
        ui.treeTabs[idx].hasFoundRecord = false;
        return;
    }

    if (res->status != 200) {
        ui.treeTabs[idx].status = res->body;
        ui.treeTabs[idx].hasFoundRecord = false;
        return;
    }

    auto json = nlohmann::json::parse(res->body);
    ui.treeTabs[idx].status = "Значение найдено в дереве";
    if (json.contains("record")) {
        ui.treeTabs[idx].foundRecord = json["record"];
        ui.treeTabs[idx].hasFoundRecord = true;
    } else {
        ui.treeTabs[idx].hasFoundRecord = false;
    }
}

static void deleteRecord(UiState& ui, int id) {
    httplib::Client cli("127.0.0.1", 8080);

    auto res = cli.Post(
        ("/api/sports/delete?id=" + std::to_string(id)).c_str(),
        "",
        "application/json"
    );

    if (!res) {
        ui.status = "Ошибка удаления записи";
        return;
    }

    ui.status = res->body;

    int maxPageBefore = std::max(1, (ui.total + ui.limit - 1) / ui.limit);
    if (ui.page > maxPageBefore) ui.page = maxPageBefore;

    fetchPage(ui);
}

static void addRecord(UiState& ui) {
    httplib::Client cli("127.0.0.1", 8080);

    int id = std::atoi(ui.sportId);
    std::string finalImagePath;

    if (!ui.selectedImageSourcePath.empty()) {
        finalImagePath = copyImageToAssets(ui.selectedImageSourcePath, id);
    }

    if (finalImagePath.empty()) {
        finalImagePath = std::string(ui.imagePath);
    }

    nlohmann::json body = {
        {"sport_id", id},
        {"name", std::string(ui.name)},
        {"category", std::string(ui.category)},
        {"olympic_status", ui.olympic},
        {"description", std::string(ui.description)},
        {"governing_body", std::string(ui.governingBody)},
        {"image_path", finalImagePath},
        {"medical_contraindications", std::string(ui.contraindications)}
    };

    auto res = cli.Post("/api/sports", body.dump(), "application/json");

    if (!res) {
        ui.status = "Ошибка добавления записи";
        return;
    }

    ui.status = res->body;

    if (res->status == 200) {
        clearAddForm(ui);
        fetchPage(ui);
    }
}

static void drawTreeTextInOrder(const nlohmann::json& node, int depth = 0) {
    if (node.is_null()) return;

    drawTreeTextInOrder(node["left"], depth + 1);

    std::string prefix(depth * 4, ' ');
    std::string line = prefix +
        node["display_value"].get<std::string>() +
        " [id=" + std::to_string(node["sport_id"].get<int>()) + "]" +
        " (w:" + std::to_string(node["weight"].get<int>()) + ")";

    ImGui::TextUnformatted(line.c_str());

    drawTreeTextInOrder(node["right"], depth + 1);
}

static void drawRecordCard(const nlohmann::json& rec, bool showDeleteButton, UiState* ui = nullptr) {
    ImGui::Separator();

    int id = rec.value("sport_id", 0);
    std::string name = rec.value("name", "");
    std::string category = rec.value("category", "");
    bool olympic = rec.value("olympic_status", false);
    std::string description = rec.value("description", "");
    std::string body = rec.value("governing_body", "");
    std::string imagePath = rec.value("image_path", "");
    std::string contraindications = rec.value("medical_contraindications", "");
    int weight = rec.value("weight", 1);

    ImGui::Text("ID: %d", id);
    ImGui::TextWrapped("Name: %s", name.c_str());
    ImGui::TextWrapped("Category: %s", category.c_str());
    ImGui::Text("Olympic: %s", olympic ? "true" : "false");
    ImGui::TextWrapped("Description: %s", description.c_str());
    ImGui::TextWrapped("Governing body: %s", body.c_str());
    ImGui::TextWrapped("Image path: %s", imagePath.c_str());
    ImGui::TextWrapped("Contraindications: %s", contraindications.c_str());
    ImGui::Text("Weight: %d", weight);

    drawImagePreview(imagePath);

    if (showDeleteButton && ui) {
        std::string buttonId = "Удалить##" + std::to_string(id);
        if (ImGui::Button(buttonId.c_str())) {
            deleteRecord(*ui, id);
        }
    }
}

int main() {
    std::filesystem::create_directories("data");
    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("assets/images");
    std::filesystem::create_directories("assets/fonts");

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
            int maxPage = std::max(1, (ui.total + ui.limit - 1) / ui.limit);
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

        ImGui::InputText("Имя для binary search v2", ui.searchName, sizeof(ui.searchName));
        if (ImGui::Button("Поиск (binary search v2)")) {
            doBinarySearch(ui);
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
        if (ImGui::Button("Выбрать фото")) {
            chooseImage(ui);
        }

        if (!ui.selectedImageSourcePath.empty()) {
            ImGui::TextWrapped("Выбран файл: %s", ui.selectedImageSourcePath.c_str());
        }

        ImGui::InputTextMultiline("medical_contraindications", ui.contraindications, sizeof(ui.contraindications), ImVec2(-1, 80));

        if (ImGui::Button("Добавить и сохранить")) {
            addRecord(ui);
        }
        ImGui::End();

        ImGui::Begin("Результат binary search v2");
        if (ui.hasBinarySearchRecord) {
            drawRecordCard(ui.binarySearchRecord, false);
        } else {
            ImGui::TextUnformatted("Пока ничего не найдено.");
        }
        ImGui::End();

        ImGui::Begin("Записи");
        for (const auto& rec : ui.records) {
            drawRecordCard(rec, true, &ui);
        }
        ImGui::End();

        ImGui::Begin("Дерево A1");

        if (ImGui::BeginTabBar("TreeTabs")) {
            for (size_t i = 0; i < g_treeFields.size(); ++i) {
                SortField field = g_treeFields[i];
                const char* label = g_treeLabels[i];
                TreeTabState& tab = ui.treeTabs[i];

                if (ImGui::BeginTabItem(label)) {
                    std::string buildBtn = std::string("Построить дерево##") + label;
                    if (ImGui::Button(buildBtn.c_str())) {
                        buildTreeForField(ui, field);
                    }

                    ImGui::InputText((std::string("Значение для поиска##") + label).c_str(),
                                     tab.searchValue,
                                     sizeof(tab.searchValue));

                    std::string searchBtn = std::string("Поиск в дереве##") + label;
                    if (ImGui::Button(searchBtn.c_str())) {
                        searchInTree(ui, field);
                    }

                    ImGui::Separator();
                    ImGui::TextWrapped("Статус вкладки: %s", tab.status.c_str());
                    ImGui::Separator();

                    if (!tab.tree.is_null()) {
                        ImGui::TextUnformatted("Обход слева-направо:");
                        ImGui::BeginChild((std::string("TreeView##") + label).c_str(), ImVec2(0, 250), true);
                        drawTreeTextInOrder(tab.tree);
                        ImGui::EndChild();
                    } else {
                        ImGui::TextUnformatted("Пока дерево не построено.");
                    }

                    ImGui::Separator();

                    if (tab.hasFoundRecord) {
                        ImGui::TextUnformatted("Найденная запись:");
                        drawRecordCard(tab.foundRecord, false);
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    destroyAllTextures();

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
