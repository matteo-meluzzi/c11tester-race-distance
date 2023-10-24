#include "relationsgraph.h"
#include <queue>

using namespace std;

typedef pair<int, const RelationsGraphNode *> PairDistNode;

void RelationsGraph::addEdge(const ModelAction *from_node, const RelationGraphEdge &edge) {
    node_to_edges[from_node].push_back(edge);
}

int RelationsGraph::minDistanceBetween(const ModelAction *from, const ModelAction *to) {
    unordered_map<const RelationsGraphNode *, int> distances_table;
    
    priority_queue<PairDistNode, vector<PairDistNode>, greater<PairDistNode>> pq; // min-heap
    pq.push({0, from});

    while (!pq.empty()) {
        auto top = pq.top();
        auto dist_u = top.first;
        auto u = top.second;

        pq.pop();

        if (distances_table.count(u) == 0)
            distances_table[u] = numeric_limits<int>::max();
        
        if (u == to)
            return dist_u; // minimal distance found
        
        if (dist_u > distances_table[u])
            continue; // skip because this is not the shortest path to u
        
        auto u_edges = node_to_edges[u];
        for (auto n_index = 0; n_index < u_edges.size(); n_index++) {
            auto v = u_edges[n_index].to_node;

            int new_dist = dist_u + 1;
            if (new_dist < distances_table[v]) {
                distances_table[v] = new_dist;
                pq.push({new_dist, v});
            }
        }
    }
}

/*
 * DFS starting in 'from' looking for 'to'
 * keeps intermediate 'visited' and 'current_path' with the same elements but one in a set, one in a vector
 * 'visited' and 'current_path' are cloned at every recursive call (expensive) because we enumerate all paths
 * they are needed to avoid cycling indefinetely
 * all the paths shorter than k are stored in 'result' (passed by reference)
 */
void RelationsGraph::allPathsShorterThanHelper(RelationsGraphNode *from, 
                                                RelationsGraphNode *to, 
                                                int k, 
                                                std::vector<RelationsGraphPath> &result, 
                                                std::unordered_set<RelationsGraphNode *> visited, 
                                                std::vector<RelationsGraphNode *> current_path) {
    if (from == to) {
        result.push_back(current_path);
        return;
    }

    if (current_path.size() == k)
        return;

    visited.insert(from);
    current_path.push_back(from);

    auto from_edges = node_to_edges[from];
    for (auto n_index = 0; n_index < from_edges.size(); n_index++) {
        auto v = from_edges[n_index].to_node;
        if (visited.count(v) == 0)
            allPathsShorterThanHelper(v, to, k, result, visited, current_path);
    }
}


vector<RelationsGraphPath> RelationsGraph::allPathsShorterThan(const ModelAction *from, const ModelAction *to, int k) {
    vector<RelationsGraphPath> xs;
    
    allPathsShorterThanHelper(from, to, k, xs);

    return xs;
}