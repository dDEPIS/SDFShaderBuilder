#pragma once
#include "NodeGraph.hpp"
#include <string>

class WgslCompiler {
public:
    // Generates the full WGSL script, including uniforms, vertex shader, and fragment/compute logic
    std::string Compile(const NodeGraph& graph);

private:
    // Recursive or topological traversal step
    std::string TraverseNode(const NodeGraph& graph, const Node* node, std::string& outDeclarations);

    // Specialized generators
    std::string GenerateSphereSDF(const Node* node);
    std::string GenerateSmoothUnion(const Node* node);

    // Finds the upstream node connected to a specific input pin
    const Node* GetConnectedNode(const NodeGraph& graph, int inputPinId) const;
};