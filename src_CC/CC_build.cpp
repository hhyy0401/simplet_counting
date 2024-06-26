#include "CC.h"
#include<vector>
#include<random>
#include<iostream>
#include<iomanip>
#include<map>
#include<bitset>
#include<math.h>
#include<typeinfo>
#include<omp.h>
#include<algorithm>

#define LIMIT 3372036854775807 // 9223372036854775807

using namespace std;

CC::CC(int k, int V, vector<vector<int>> simplices, vector<vector<int>> v_to_sim){ //initialization: asign color for each vertex
    
    // common
    this->k=k;
    this->V=V;

    this -> simplices = simplices;
    this -> vertex_to_simplices = v_to_sim;
    this -> n_threads = omp_get_max_threads();
    

    initialize_motifs();

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dis(0, this->k-1);
    colors=(1 << k);
    color.resize(V);
    for(int i=0; i<V; i++){ //$
        color[i]=dis(gen);
    }

    num_ones.resize(colors);
    color_idx.resize(colors);
    bin_ones.resize(k);
    // new
    //printf("DEBUG: num_ones");
    for(int i=1;i<colors;i++){
        num_ones[i] = num_ones[i & (i-1)] + 1;
        //printf(" %d", num_ones[i]);
        color_idx[i] = bin_ones[num_ones[i]-1].size();
        bin_ones[num_ones[i]-1].push_back(i);
    }
    color_pairs.resize(k);
    for(int i=0;i<k;i++){
        color_pairs[i].resize(k);
    }

    for(int i=1;i<colors-1;i++){
        for(int j=1;j<colors-1;j++){
            if(i & j) continue;
            color_pairs[num_ones[i]][num_ones[j]].push_back({i, j});
        }
    }

    C.resize(V);
    
    create_tree_table();
    //cout << "READY" << endl;
}

void CC::create_project_graph(){ //change this


    adj_list.resize(V);
    for(int i=0; i<simplices.size(); i++){
        int size = simplices[i].size();
        for(int j=0; j<size-1; j++){
            for(int l=j+1; l<size; l++){
                int u = simplices[i][j];
                int v = simplices[i][l];
                if(u!=v){
                    adj_list[u].push_back(v);
                    adj_list[v].push_back(u);
                }
            }
        }
    }

    for(int i=0; i<V; i++){
        sort(adj_list[i].begin(), adj_list[i].end());
        adj_list[i].erase(unique(adj_list[i].begin(), adj_list[i].end()), adj_list[i].end());
    }


    return;
}


void CC::build(){ //build color table

    memoized_rng.resize(V);
    for(int i=0;i<V;i++){
        C[i].resize(cumul[k-1] + num[k-1]);
        memoized_rng[i].resize(cumul[k-1] + num[k-1]);
        for(int sz=1;sz<=k;sz++){
            for(int j=cumul[sz-1];j<cumul[sz-1] + num[sz-1];j++){
                C[i][j].resize((int)bin_ones[sz-1].size());
                memoized_rng[i][j].resize((int)bin_ones[sz-1].size());
            }
        }
        C[i][0][color[i]] = 1;
    }
    
    
    for(int curr_k=2;curr_k<=k;curr_k++){
        
        int num_subtasks = V * num[curr_k-1];
        //cout << "curr_k=" << curr_k << " start!" << endl;
        #pragma omp parallel for
        for(int task_i = 0; task_i < num_subtasks ;task_i++){
            
            int v = task_i / num[curr_k-1], dtype = cumul[curr_k-1] + (task_i % num[curr_k-1]); 
            if((curr_k < k) || (!color[v])){
                update(v, dtype);
            }
        }
        //cout << "curr_k=" << curr_k << " done." << endl;
    }
    rng_root.resize(V * num[k-1]);
    int decomp_from_2nd = cumul[k-1], decomp_to_2nd = cumul[k-1] + num[k-1];   
    count_type cumul_counts = 0;
    double _start2 = omp_get_wtime();
    for(int v=0;v<V;v++){
        for(int i=0;i<num[k-1];i++){
            cumul_counts += C[v][cumul[k-1] + i][0];
            rng_root[v * num[k-1] + i] = cumul_counts;
        }
    }
    //cout << (omp_get_wtime() - _start2) << " New:" << cumul_counts << endl;
}


void CC::update(int v, int treelet_id){ //update color table
    for(auto &u: adj_list[v]){
        for(auto &decomp_case: D[treelet_id]){ //candidate decomposition of T as T1 and T2
            int main_num_nodes = D[decomp_case.main_id][0].num_nodes;
            for(auto &cs: color_pairs[main_num_nodes][decomp_case.num_nodes - main_num_nodes]){
                C[v][treelet_id][color_idx[cs.first | cs.second]] += C[v][decomp_case.main_id][color_idx[cs.first]] * C[u][decomp_case.sub_id][color_idx[cs.second]];
                memoized_rng[v][treelet_id][color_idx[cs.first | cs.second]].capacity += 1;
            }
        }
    }
    for(auto &target_color: bin_ones[D[treelet_id][0].num_nodes-1]){
        C[v][treelet_id][color_idx[target_color]] /= D[treelet_id][0].deg_root;
    }
}

void CC::create_tree_table(){

    if(k == 0) return;

    D.push_back(vector<CC::decomp_type>()); // 0: base (with one node)
    D[0].push_back({1, -1, -1, 1});
    if(k == 1) return;
    
    D.push_back(vector<CC::decomp_type>()); // 1: root - v, basic tree with two nodes
    D[1].push_back({2, 0, 0, 1});
    if(k == 2) return;
    
    D.push_back(vector<CC::decomp_type>()); // 2: root - v - v
    D[2].push_back({3, 0, 1, 1});
    D.push_back(vector<CC::decomp_type>()); // 3: v - root - v (like wedge)
    D[3].push_back({3, 1, 0, 2});
    if(k == 3) return;
    
    D.push_back(vector<CC::decomp_type>()); // 4: root - v - v - v
    D[4].push_back({4, 0, 2, 1});
    D.push_back(vector<CC::decomp_type>()); // 5: root - wedge(index_3)
    D[5].push_back({4, 0, 3, 1});
    D.push_back(vector<CC::decomp_type>()); // 6: v - v - root - v 
    D[6].push_back({4, 1, 1, 2}); 
    D[6].push_back({4, 2, 0, 2}); // or v - root - v - v
    D.push_back(vector<CC::decomp_type>()); 
    D[7].push_back({4, 3, 0, 3}); // 7: root-v,v,v (three nodes are all children)
    if(k == 4) return;
    
    D.push_back(vector<CC::decomp_type>()); // 8: root-v-v-v-v 
    D[8].push_back({5, 0, 4, 1}); 
    D.push_back(vector<CC::decomp_type>()); // 9: v-root-v-v-v 
    D[9].push_back({5, 4, 0, 2});
    D[9].push_back({5, 1, 2, 2});
    D.push_back(vector<CC::decomp_type>()); // 10: v-v-root-v-v 
    D[10].push_back({5, 2, 1, 2});
    D.push_back(vector<CC::decomp_type>()); // 11: root - (v - v - subroot - v, 6)
    D[11].push_back({5, 0, 6, 1});
    D.push_back(vector<CC::decomp_type>()); // 12: 7 and one additional leaf at one of the leaf of 7 (root-(v-v),v,v)
    D[12].push_back({5, 6, 0, 3});
    D[12].push_back({5, 3, 1, 3});
    D.push_back(vector<CC::decomp_type>()); // 13: root - wedge, v 
    D[13].push_back({5, 1, 3, 2});
    D[13].push_back({5, 5, 0, 2});
    D.push_back(vector<CC::decomp_type>()); // 14: root-v-v-(v,v) 
    D[14].push_back({5, 0, 5, 1});
    D.push_back(vector<CC::decomp_type>()); // 15: root-v,v,v,v (four nodes are all children) 
    D[15].push_back({5, 7, 0, 4}); 
    D.push_back(vector<CC::decomp_type>()); // 16: root-v-(v,v,v) 
    D[16].push_back({5, 0, 7, 1});
    if(k == 5) return;

    D.push_back(vector<CC::decomp_type>()); 
    D[17].push_back({6, 0, 8, 1}); // 17: path
    D.push_back(vector<CC::decomp_type>()); 
    D[18].push_back({6, 2, 2, 2}); // 18: path v-v-v-root-v-v
    D[18].push_back({6, 4, 1, 2}); // 18: path v-v-root-v-v-v
    D.push_back(vector<CC::decomp_type>()); 
    D[19].push_back({6, 1, 4, 2}); // 19: path v-v-v-v-root-v
    D[19].push_back({6, 8, 0, 2}); // 19: path v-root-v-v-v-v
    D.push_back(vector<CC::decomp_type>()); 
    D[20].push_back({6, 0, 11, 1}); // 20: root-v-(v-v-subroot-v)
    D.push_back(vector<CC::decomp_type>()); 
    D[21].push_back({6, 0, 10, 1}); // 21: root-(v-v-subroot-v-v)
    D.push_back(vector<CC::decomp_type>()); 
    D[22].push_back({6, 1, 6, 2}); // 22: (v-v-subroot-v)-root-v
    D[22].push_back({6, 11, 0, 2}); // 22: v-root-(v-v-subroot-v)
    D.push_back(vector<CC::decomp_type>()); 
    D[23].push_back({6, 10, 0, 3}); // 23: root-(1,2,2) 
    D[23].push_back({6, 6, 1, 3}); // 23: root-(2,2,1)
    D.push_back(vector<CC::decomp_type>()); 
    D[24].push_back({6, 0, 14, 1}); // 24: root-v-v-wedge
    D.push_back(vector<CC::decomp_type>()); 
    D[25].push_back({6, 1, 5, 2}); // 25: wedge-wedge    
    D[25].push_back({6, 14, 0, 2}); // 25: wedge-wedge
    D.push_back(vector<CC::decomp_type>()); 
    D[26].push_back({6, 0, 9, 1}); // 26: v-(4,1)
    D.push_back(vector<CC::decomp_type>()); 
    D[27].push_back({6, 2, 3, 2}); // 27: wedge-root-v-v   
    D[27].push_back({6, 5, 1, 2}); // 27: v-v-root-wedge
    D.push_back(vector<CC::decomp_type>()); 
    D[28].push_back({6, 3, 2, 3}); // 28: root-(3,1,1)   
    D[28].push_back({6, 9, 0, 3}); // 28: root-(1,3,1)  
    D.push_back(vector<CC::decomp_type>()); 
    D[29].push_back({6, 0, 13, 1}); // 29: root-(wedge-subroot-v)
    D.push_back(vector<CC::decomp_type>()); 
    D[30].push_back({6, 3, 3, 3}); // 30: root-(wedge,v,v)
    D[30].push_back({6, 13, 0, 3}); // 30: root-(v,wedge,v) 
    D.push_back(vector<CC::decomp_type>()); 
    D[31].push_back({6, 16, 0, 3}); // 31: root-v-v-(v,v,v)
    D.push_back(vector<CC::decomp_type>()); 
    D[32].push_back({6, 0, 12, 1}); // 32: root-v-(2,1,1)
    D.push_back(vector<CC::decomp_type>()); 
    D[33].push_back({6, 1, 7, 2}); // 33: root-(subroot-(v,v,v),v)
    D[33].push_back({6, 16, 0, 2}); // 33: root-(v, subroot-(v,v,v))
    D.push_back(vector<CC::decomp_type>()); 
    D[34].push_back({6, 7, 1, 4}); // 34: root-(2,1,1,1)
    D[34].push_back({6, 12, 0, 4}); // 34: root-(1,2,1,1)
    D.push_back(vector<CC::decomp_type>()); 
    D[35].push_back({6, 0, 15, 1}); // 35: root-v-(v,v,v,v)
    D.push_back(vector<CC::decomp_type>()); 
    D[36].push_back({6, 15, 0, 5}); // 36: root-(v,v,v,v,v)
    if(k == 6) return;
    
}
