#include "relationsgraph.h"
#include <queue>
#include "action.h"

using namespace std;

typedef pair<int, const RelationsGraphNode *> PairDistNode;

void RelationsGraph::addEdge(const ModelAction *from_node, const RelationGraphEdge &edge) {
    node_to_edges[from_node].push_back(edge);
}

int RelationsGraph::minDistanceBetween(const ModelAction *from, const ModelAction *to) const {
    unordered_map<const RelationsGraphNode *, int> distances_table;
    distances_table[from] = 0;

    ASSERT(node_to_edges.count(from) > 0);

    // model_print("%p to %p\n", from, to);

    // model_print("edges of from_node:\n");
    // for (auto e : node_to_edges.at(from)) {
    //     model_print("edge to %d %p\n", e.to_node->get_seq_number(), e.to_node);
    // }
    
    priority_queue<PairDistNode, vector<PairDistNode>, greater<PairDistNode>> pq; // min-heap
    pq.push({0, from});

    while (!pq.empty()) {
        auto top = pq.top();
        auto dist_u = top.first;
        auto u = top.second;

        // model_print("enumerating node %d\n", u->get_seq_number());

        pq.pop();

        if (distances_table.count(u) == 0) {
            // model_print("distances table does not contain %d\n", u->get_seq_number());
            distances_table[u] = numeric_limits<int>::max();
        }
        
        if (u == to) {
            // model_print("relations_graph: found a path with distance %d\n", dist_u);
            return dist_u; // minimal distance found
        }
        
        if (dist_u > distances_table[u]) {
            // model_print("current distance (%d) greater than distances_table[u] (%d)\n", dist_u, distances_table[u]);
            continue; // skip because this is not the shortest path to u
        }
        
        if (node_to_edges.count(u) == 0) {
            // model_print("relations_graph: reached a node with no edges\n");
            continue;
        }
        auto u_edges = node_to_edges.at(u);
        // model_print("u has %d edges\n", u_edges.size());
        for (size_t n_index = 0; n_index < u_edges.size(); n_index++) {
            auto v = u_edges[n_index].to_node;

            int new_dist = dist_u + 1;
            if (distances_table.count(v) == 0) 
                distances_table[v] = numeric_limits<int>::max();
            if (new_dist < distances_table.at(v)) {
                // model_print("added node %d to pq\n", v->get_seq_number());
                distances_table[v] = new_dist;
                pq.push({new_dist, v});
            }
        }
    }
    // model_print("relations_graph: could not find a path\n");
    return -1; // no path between them was found
}

/*
 * DFS starting in 'from' looking for 'to'
 * keeps intermediate 'visited' and 'current_path' with the same elements but one in a set, one in a vector
 * 'visited' and 'current_path' are cloned at every recursive call (expensive) because we enumerate all paths
 * they are needed to avoid cycling indefinetely
 * all the paths shorter than k are stored in 'result' (passed by reference)
 */
void RelationsGraph::allPathsShorterThanHelper(const RelationsGraphNode *from,
                                                const RelationsGraphNode *to, 
                                                size_t k, 
                                                std::vector<RelationsGraphPath> &result, 
                                                RelationGraphEdgeType followed_edge_type,
                                                std::unordered_set<const RelationsGraphNode *> visited, 
                                                std::vector<RelationsGraphPathComponent> current_path) const {
    // model_print("visiting node %d, current path size: %d\n", from->get_seq_number(), current_path.size());
    if (from == to) {
        current_path.push_back({from, followed_edge_type});
        result.push_back(current_path);
        return;
    }

    if (current_path.size() == k) {
        return;
    }

    visited.insert(from);
    current_path.push_back({from, followed_edge_type});

    if (node_to_edges.count(from) == 0) 
        return;
    auto from_edges = node_to_edges.at(from);
    for (size_t n_index = 0; n_index < from_edges.size(); n_index++) {
        auto v = from_edges[n_index].to_node;
        if (visited.count(v) == 0)
            allPathsShorterThanHelper(v, to, k, result, from_edges[n_index].type, visited, current_path);
    }
}


vector<RelationsGraphPath> RelationsGraph::allPathsShorterThan(const ModelAction *from, const ModelAction *to, int k) const {
    vector<RelationsGraphPath> xs;
    
    allPathsShorterThanHelper(from, to, k, xs, static_cast<RelationGraphEdgeType>(-1));

    return xs;
}

const char * pretty_edge_type(const RelationGraphEdgeType type) {
    switch (type)
    {
    case READ_FROM:
        return "READ_FROM";
    case HAPPENS_BEFORE:
        return "HAPPENS_BEFORE";
    case SEQUENTIAL_CONSISTENCY:
        return "SEQUENTIAL_CONSISTENCY";
    default:
        return "UNKNOWN_EDGE_TYPE";
    }
}

const char * pretty_edge_type(const RelationGraphEdge &e) {
    return pretty_edge_type(e.type);
}


string pretty_node_type(const RelationsGraphNode *n) {
    switch (n->get_type()) 
    {
        case ATOMIC_INIT:
            return "ATOMIC_INIT";
        case ATOMIC_WRITE:
            return "ATOMIC_WRITE";
        case ATOMIC_READ:
            return "ATOMIC_READ";
        case NONATOMIC_WRITE:
            return "NONATOMIC_WRITE";
        case THREAD_CREATE:
            return "THREAD_CREATE";
        case THREAD_JOIN:
            return "THREAD_JOIN";
        case THREAD_START:
            return "THREAD_START";
        case THREAD_FINISH:
            return "THREAD_FINISH";
        default:
            return "ANOTHER_TYPE: " + to_string(n->get_type());
    }
}

void RelationsGraph::pretty_print() {
    model_print("RELATIONS GRAPH of size %d:\n", node_to_edges.size());
    for (auto node_edges : node_to_edges) {
        auto node = node_edges.first;
        auto edges = node_edges.second;
        model_print("node with seq num %d (%s):\n", node->get_seq_number(), pretty_node_type(node).c_str());
        for (auto e : edges) {
            model_print("\t %s -> %d (%s)\n", pretty_edge_type(e), e.to_node->get_seq_number(), pretty_node_type(e.to_node).c_str());
        }
        model_print("\n");
    }
}