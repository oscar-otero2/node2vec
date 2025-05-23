#include "stdafx.h"
#include "n2v.h"
#include <ctime>

// Base function of node2vec
void node2vec(PWNet& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  const bool& OutputWalks, TVVec<TInt, int64>& WalksVV,
  TIntFltVH& EmbeddingsHV) {
  // Preprocess transition probabilities
  // From biasedrandomwalk.cpp ->
  // Is essentially section 3.2.2 of node2vec's paper.
  
  clock_t begin = clock();
  double begin_nat = omp_get_wtime();
  PreprocessTransitionProbs(InNet, ParamP, ParamQ, Verbose);
  clock_t end = clock();
  double end_nat = omp_get_wtime();
  double _time = double(end-begin)/CLOCKS_PER_SEC;
  double _time_nat = end_nat - begin_nat;
  // printf("PreprocessTransitionProbs: %fs\n", _time);
  // printf("PreprocessTransitionProbs NAT: %fs\n", _time_nat);
  printf("<preprocess_transition_probs process=\"%f\" natural=\"%f\" />", _time, _time_nat);


  // This is getting all ids form InNet
  // The algorithm's input graph
  TIntV NIdsV;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    NIdsV.Add(NI.GetId());
  }

  // Prepare to generate random walks
  int64 AllWalks = (int64)NumWalks * NIdsV.Len();
  WalksVV = TVVec<TInt, int64>(AllWalks,WalkLen);
  //TRnd Rnd(time(NULL));
  TRnd Rnd(1);
  int64 WalksDone = 0;

  // For all walks to be generated
  
  begin = clock();
  begin_nat = omp_get_wtime();
  for (int64 i = 0; i < NumWalks; i++) {

    // This function litteraly shuffles the nodes in the vector
    NIdsV.Shuffle(Rnd);

    // This precompiler option schedules threads (openmp directive)
    // Applies for this for loop (iterates over all nodes)
//#pragma omp parallel for schedule(dynamic)
    for (int64 j = 0; j < NIdsV.Len(); j++) {
      
      // Prints by little the progress
      if ( Verbose && WalksDone%10000 == 0 ) {
        printf("\rWalking Progress: %.2lf%%",(double)WalksDone*100/(double)AllWalks);fflush(stdout);
      }

      // Walks are the sucession of nodes in a vector
      TIntV WalkV;
      
      // !!
      SimulateWalk(InNet, NIdsV[j], WalkLen, Rnd, WalkV);
      
      // For each node visited write it into WalksVV
      for (int64 k = 0; k < WalkV.Len(); k++) { 
        WalksVV.PutXY(i*NIdsV.Len()+j, k, WalkV[k]);
      }
      WalksDone++;
    }
  }
  end = clock();
  end_nat = omp_get_wtime();
  _time = double(end-begin)/CLOCKS_PER_SEC;
  _time_nat = end_nat - begin_nat;
  // printf("All SimulateWalk: %fs\n", _time);
  // printf("All SimulateWalk NAT: %fs\n", _time_nat);
  printf("<simulate_walk process=\"%f\" natural=\"%f\" />", _time, _time_nat);
  
  // I think this clears the progress line (?)
  if (Verbose) {
    printf("\n");
    fflush(stdout);
  }
  // Learning embeddings
  // Don't even bother if they will not be used
  // Function from word2vec.cpp ->
  // 
  if (!OutputWalks) {
    begin = clock();
    begin_nat = omp_get_wtime();
    LearnEmbeddings(WalksVV, Dimensions, WinSize, Iter, Verbose, EmbeddingsHV);
    end = clock();
    end_nat = omp_get_wtime();
    _time = double(end-begin)/CLOCKS_PER_SEC;
    _time_nat = end_nat - begin_nat;
    // printf("LearnEmbeddings: %fs\n", _time);
    // printf("LearnEmbeddings NAT: %fs\n", _time_nat);
    printf("<learn_embeddings process=\"%f\" natural=\"%f\" />", _time, _time_nat);
  }
}


/*
  From here, everything is just an overloading
  of the function node2vec so that it can be invoked
  with a different amount of parameters.
  They will be studied but those changes are not so relevant
  to the inner workings of this algorithm.
*/


void node2vec(PWNet& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  TIntFltVH& EmbeddingsHV) {
  TVVec <TInt, int64> WalksVV;
  bool OutputWalks = 0;
  node2vec(InNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize,
   Iter, Verbose, OutputWalks, WalksVV, EmbeddingsHV);
}


void node2vec(const PNGraph& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  const bool& OutputWalks, TVVec<TInt, int64>& WalksVV,
  TIntFltVH& EmbeddingsHV) {
  PWNet NewNet = PWNet::New();
  for (TNGraph::TEdgeI EI = InNet->BegEI(); EI < InNet->EndEI(); EI++) {
    if (!NewNet->IsNode(EI.GetSrcNId())) { NewNet->AddNode(EI.GetSrcNId()); }
    if (!NewNet->IsNode(EI.GetDstNId())) { NewNet->AddNode(EI.GetDstNId()); }
    NewNet->AddEdge(EI.GetSrcNId(), EI.GetDstNId(), 1.0);
  }
  node2vec(NewNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize, Iter, 
   Verbose, OutputWalks, WalksVV, EmbeddingsHV);
}

void node2vec(const PNGraph& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  TIntFltVH& EmbeddingsHV) {
  TVVec <TInt, int64> WalksVV;
  bool OutputWalks = 0;
  node2vec(InNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize,
   Iter, Verbose, OutputWalks, WalksVV, EmbeddingsHV);
}

void node2vec(const PNEANet& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  const bool& OutputWalks, TVVec<TInt, int64>& WalksVV,
  TIntFltVH& EmbeddingsHV) {
  PWNet NewNet = PWNet::New();
  for (TNEANet::TEdgeI EI = InNet->BegEI(); EI < InNet->EndEI(); EI++) {
    if (!NewNet->IsNode(EI.GetSrcNId())) { NewNet->AddNode(EI.GetSrcNId()); }
    if (!NewNet->IsNode(EI.GetDstNId())) { NewNet->AddNode(EI.GetDstNId()); }
    NewNet->AddEdge(EI.GetSrcNId(), EI.GetDstNId(), InNet->GetFltAttrDatE(EI,"weight"));
  }
  node2vec(NewNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize, Iter, 
   Verbose, OutputWalks, WalksVV, EmbeddingsHV);
}

void node2vec(const PNEANet& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
 TIntFltVH& EmbeddingsHV) {
  TVVec <TInt, int64> WalksVV;
  bool OutputWalks = 0;
  node2vec(InNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize,
   Iter, Verbose, OutputWalks, WalksVV, EmbeddingsHV);
}

