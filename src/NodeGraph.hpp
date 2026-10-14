#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

enum class PinType {
    Float,
    Vec3,
    SDF,      // Carries distance field data
    Material  // Carries BRDF/shading data
};

enum class NodeType {
    PrimitiveSphere,
    PrimitiveBox,
    OpSmoothUnion,
    OpSubtract,
    Output
};

struct Pin {
    int id;
    int nodeId;
    PinType type;
    std::string name;
};

struct Link {
    int id;
    int startPinId; // Output pin
    int endPinId;   // Input pin
};

struct Node {
    int id;
    NodeType type;
    std::string name;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // Node-specific payload (e.g., sphere radius, box bounds)
    std::unordered_map<std::string, float> floatParams;
    std::unordered_map<std::string, std::vector<float>> vecParams;
};

class NodeGraph {
public:
    int AddNode(NodeType type);
    void AddLink(int startPinId, int endPinId);
    void RemoveLink(int linkId);
    void RemoveNode(int nodeId);

    const Node* GetNode(int id) const;
    const std::vector<Node>& GetNodes() const { return nodes; }
    const std::vector<Link>& GetLinks() const { return links; }

    // Returns the root output node for the compiler to begin traversal
    const Node* GetOutputNode() const;

private:
    std::vector<Node> nodes;
    std::vector<Link> links;
    int nextId = 1;
};