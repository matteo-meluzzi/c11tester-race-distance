#ifndef __RELATIONSGRAPH_H__
#define __RELATIONSGRAPH_H__

#include <unordered_map>
#include <vector>
#include "action.h"
#include <unordered_set>

using RelationsGraphNode = ModelAction;
using RelationsGraphPath = std::vector<const RelationsGraphNode *>;

typedef enum {
    READ_FROM,
    HAPPENS_BEFORE,
    SEQUENTIAL_CONSISTENCY
} RelationGraphEdgeType;

struct RelationGraphEdge {
    RelationGraphEdgeType type;
    RelationsGraphNode *to_node;
};

class RelationsGraph {
public:
    void addEdge(const ModelAction *from_node, const RelationGraphEdge &edge);

    int minDistanceBetween(const ModelAction *from, const ModelAction *to) const;
    std::vector<RelationsGraphPath> allPathsShorterThan(const ModelAction *from, const ModelAction *to, int k) const;
private:
    std::unordered_map<const RelationsGraphNode *, std::vector<RelationGraphEdge>> node_to_edges; 

    void allPathsShorterThanHelper(const RelationsGraphNode *from, 
                                   const RelationsGraphNode *to, 
                                   size_t k, 
                                   std::vector<RelationsGraphPath> &result, 
                                   std::unordered_set<const RelationsGraphNode *> visited = std::unordered_set<const RelationsGraphNode *>(), 
                                   std::vector<const RelationsGraphNode *> current_path = std::vector<const RelationsGraphNode *>()) const;
};

#endif