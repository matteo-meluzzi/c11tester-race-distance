#ifndef __RELATIONSGRAPH_H__
#define __RELATIONSGRAPH_H__

#include <unordered_map>
#include <vector>
#include <unordered_set>

class ModelAction;

using RelationsGraphNode = ModelAction;

typedef enum {
    READ_FROM,
    HAPPENS_BEFORE,
    SEQUENTIAL_CONSISTENCY
} RelationGraphEdgeType;

struct RelationsGraphPathComponent {
    const RelationsGraphNode *node;
    RelationGraphEdgeType edge_type;
};
using RelationsGraphPath = std::vector<RelationsGraphPathComponent>;

struct RelationGraphEdge {
    RelationGraphEdgeType type;
    RelationsGraphNode *to_node;

    RelationGraphEdge(RelationGraphEdgeType type, RelationsGraphNode *to_node) : type(type), to_node(to_node) {}
};

class RelationsGraph {
public:
    void addEdge(const ModelAction *from_node, const RelationGraphEdge &edge);

    int minDistanceBetween(const ModelAction *from, const ModelAction *to) const;
    std::vector<RelationsGraphPath> allPathsShorterThan(const ModelAction *from, const ModelAction *to, int k) const;

    void pretty_print();
private:
    std::unordered_map<const RelationsGraphNode *, std::vector<RelationGraphEdge>> node_to_edges; 

    void allPathsShorterThanHelper(const RelationsGraphNode *from, 
                                   const RelationsGraphNode *to, 
                                   size_t k, 
                                   std::vector<RelationsGraphPath> &result, 
                                   RelationGraphEdgeType followed_edge_type,
                                   std::unordered_set<const RelationsGraphNode *> visited = std::unordered_set<const RelationsGraphNode *>(), 
                                   std::vector<RelationsGraphPathComponent> current_path = std::vector<RelationsGraphPathComponent>()) const;
};

const char * pretty_edge_type(const RelationGraphEdge &e);
const char * pretty_edge_type(const RelationGraphEdgeType type);

std::string pretty_node_type(const RelationsGraphNode *n);

#endif