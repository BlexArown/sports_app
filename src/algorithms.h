#pragma once
#include "models.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>

inline bool lessByField(const Sport& a, const Sport& b, SortField field) {
    switch (field) {
        case SortField::Id: return a.sport_id < b.sport_id;
        case SortField::Name: return a.name < b.name;
        case SortField::Category: return a.category < b.category;
        case SortField::OlympicStatus: return a.olympic_status < b.olympic_status;
        case SortField::Description: return a.description < b.description;
        case SortField::GoverningBody: return a.governing_body < b.governing_body;
        case SortField::ImagePath: return a.image_path < b.image_path;
        case SortField::MedicalContraindications: return a.medical_contraindications < b.medical_contraindications;
        default: return false;
    }
}

inline void swapSports(Sport& a, Sport& b) {
    Sport tmp = a;
    a = b;
    b = tmp;
}

inline void quickSort(std::vector<Sport>& A, int L, int R, SortField field) {
    if (L >= R) return;

    Sport x = A[L];
    int i = L;
    int j = R;

    while (i <= j) {
        while (lessByField(A[i], x, field)) i++;
        while (lessByField(x, A[j], field)) j--;

        if (i <= j) {
            swapSports(A[i], A[j]);
            i++;
            j--;
        }
    }

    if (L < j) quickSort(A, L, j, field);
    if (i < R) quickSort(A, i, R, field);
}

inline int binarySearchVersion2ByName(const std::vector<Sport>& A, const std::string& X) {
    int n = static_cast<int>(A.size());
    int L = 0, R = n - 1;

    while (L < R) {
        int m = (L + R) / 2;
        if (A[m].name < X) {
            L = m + 1;
        } else {
            R = m;
        }
    }

    if (n > 0 && A[R].name == X) return R;
    return -1;
}

struct TreeNode {
    std::string key;
    int weight = 1;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
};

inline TreeNode* addVertex(TreeNode* root, const std::string& key, int weight) {
    if (root == nullptr) {
        root = new TreeNode{key, weight, nullptr, nullptr};
    } else if (key < root->key) {
        root->left = addVertex(root->left, key, weight);
    } else if (key > root->key) {
        root->right = addVertex(root->right, key, weight);
    }
    return root;
}

inline TreeNode* buildTreeByWeightA1(const std::vector<Sport>& data) {
    struct VRec {
        std::string key;
        int weight;
        bool use = false;
    };

    std::vector<VRec> V;
    V.reserve(data.size());

    for (const auto& s : data) {
        V.push_back({s.name, s.weight, false});
    }

    TreeNode* root = nullptr;

    for (size_t i = 0; i < V.size(); ++i) {
        int maxWeight = -1;
        int index = -1;

        for (size_t j = 0; j < V.size(); ++j) {
            if (!V[j].use && V[j].weight > maxWeight) {
                maxWeight = V[j].weight;
                index = static_cast<int>(j);
            }
        }

        if (index >= 0) {
            V[index].use = true;
            root = addVertex(root, V[index].key, V[index].weight);
        }
    }

    return root;
}

inline void destroyTree(TreeNode* root) {
    if (!root) return;
    destroyTree(root->left);
    destroyTree(root->right);
    delete root;
}

inline nlohmann::json treeToJson(TreeNode* root) {
    if (!root) return nullptr;
    return {
        {"key", root->key},
        {"weight", root->weight},
        {"left", treeToJson(root->left)},
        {"right", treeToJson(root->right)}
    };
}
