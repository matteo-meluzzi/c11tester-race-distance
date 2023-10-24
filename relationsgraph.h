#ifndef __RELATIONSGRAPH_H__
#define __RELATIONSGRAPH_H__

#include <unordered_map>
#include <vector>
#include "action.h"
#include <unordered_set>

// struct RelationGraphNode {
//     ModelAction *action;
// };

using RelationsGraphNode = ModelAction;
using RelationsGraphPath = std::vector<RelationsGraphNode *>;

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

    int minDistanceBetween(const ModelAction *from, const ModelAction *to);
    std::vector<RelationsGraphPath> allPathsShorterThan(const ModelAction *from, const ModelAction *to, int k);
private:
    std::unordered_map<const RelationsGraphNode *, std::vector<RelationGraphEdge>> node_to_edges; 

    void allPathsShorterThanHelper(RelationsGraphNode *from, 
                                   RelationsGraphNode *to, 
                                   int k, 
                                   std::vector<RelationsGraphPath> &result, 
                                   std::unordered_set<RelationsGraphNode *> visited = std::unordered_set<RelationsGraphNode *>(), 
                                   std::vector<RelationsGraphNode *> current_path = std::vector<RelationsGraphNode *>());
};

#endif