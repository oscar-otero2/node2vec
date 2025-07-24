#include "stdafx.h"
#include "Snap.h"
#include "biasedrandomwalk.h"

//Preprocess alias sampling method
// No info from outside of these considered neighbours is being passed arround
void GetNodeAlias(TFltV& PTblV, TIntVFltVPr& NTTable) {
  int64 N = PTblV.Len();

  // These are the vectors stored for every node
  TIntV& KTbl = NTTable.Val1;
  TFltV& UTbl = NTTable.Val2;
  
  // Init them to 0
  for (int64 i = 0; i < N; i++) {
    KTbl[i]=0;
    UTbl[i]=0;
  }

  // UnderV has indices for those neighbours where
  // Probability * num of neighbours < 1
  // The opposite is true for OverV
  TIntV UnderV;
  TIntV OverV;
  
  // For each neighbour that we evaluated
  for (int64 i = 0; i < N; i++) {
    // Float in node = probability * Num of neighbours
    UTbl[i] = PTblV[i]*N;
    if (UTbl[i] < 1) {
      UnderV.Add(i);
    } else {
      OverV.Add(i);
    }
  }

  // This is supposedly the aliasing
  while (UnderV.Len() > 0 && OverV.Len() > 0) {
    // Pop last element of both
    int64 Small = UnderV.Last();
    int64 Large = OverV.Last();
    UnderV.DelLast();
    OverV.DelLast();

    // Int vector of node
    KTbl[Small] = Large;
    
    // Flt vector of node
    UTbl[Large] = UTbl[Large] + UTbl[Small] - 1;
    
    if (UTbl[Large] < 1) {
      UnderV.Add(Large);
    } else {
      OverV.Add(Large);
    }
  }
  
  while(UnderV.Len() > 0){
    int64 curr = UnderV.Last();
    UnderV.DelLast();
    UTbl[curr]=1;
  }
  while(OverV.Len() > 0){
    int64 curr = OverV.Last();
    OverV.DelLast();
    UTbl[curr]=1;
  }

}

//Get random element using alias sampling method
int64 AliasDrawInt(TIntVFltVPr& NTTable, TRnd& Rnd) {
  int64 N = NTTable.GetVal1().Len(); // Num neighbors
  TInt X = static_cast<int64>(Rnd.GetUniDev()*N);
  double Y = Rnd.GetUniDev();
  // If Random num < float from one of the random neighbors then x, else the int from that place
  return Y < NTTable.GetVal2()[X] ? X : NTTable.GetVal1()[X];
}

// Process each node
// This is an interesting function but I don't think I need to explain it all
// Because its end goal is to give us the probabilities of taking each edge
// They are given in the node itself(?)
void PreprocessNode (PWNet& InNet, const double& ParamP, const double& ParamQ,
 TWNet::TNodeI NI, int64& NCnt, const bool& Verbose) {
  if (Verbose && NCnt%100 == 0) {
    printf("\rPreprocessing progress: %.2lf%% ",(double)NCnt*100/(double)(InNet->GetNodes()));fflush(stdout);
  }
  //for node t
  THash <TInt, TBool> NbrH;                                    //Neighbors of t
  for (int64 i = 0; i < NI.GetOutDeg(); i++) {
    NbrH.AddKey(NI.GetNbrNId(i));
  } 

  // For each neighbour
  for (int64 i = 0; i < NI.GetOutDeg(); i++) {
    TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));      //for each node v
    double Psum = 0;
    TFltV PTable;                              //Probability distribution table

    // For each neighbour's neighbours
    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {           //for each node x
      int64 FId = CurrI.GetNbrNId(j);
      TFlt Weight;
      
      // If <something> ignore x node
      // All of the values that appear after this are directly explained in the paper:
      // Section 3.2.2
      if (!(InNet->GetEDat(CurrI.GetId(), FId, Weight))){ continue; }
      if (FId==NI.GetId()) {
        PTable.Add(Weight / ParamP);
        Psum += Weight / ParamP;
      } else if (NbrH.IsKey(FId)) {
        PTable.Add(Weight);
        Psum += Weight;
      } else {
        PTable.Add(Weight / ParamQ);
        Psum += Weight / ParamQ;
      }
    }
    //Normalizing table
    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {
      PTable[j] /= Psum;
    }
    // Main result of these calculations is the PTable, unique for each node, that requires up to 2 distance neighbours for each of these
    // Only NTTAble is being modified.
    GetNodeAlias(PTable,CurrI.GetDat().GetDat(NI.GetId()));
  }
  NCnt++;
}

//Preprocess transition probabilities for each path t->v->x

///////////////////////////
// TODO: Parallelization //
///////////////////////////

void PreprocessTransitionProbs(PWNet& InNet, const double& ParamP, const double& ParamQ, const bool& Verbose) {
  // For each node in InNet
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    InNet->SetNDat(NI.GetId(),TIntIntVFltVPrH());
  }
  
  // For each node in InNet
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    // For all neighbours
    for (int64 i = 0; i < NI.GetOutDeg(); i++) {                    //allocating space in advance to avoid issues with multithreading
      TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
      // Get node data (what is it)
      // Add to it (as hashtable) (its id, a pair of an int vector and a float vector of the size of the out degree of the node)
      CurrI.GetDat().AddDat(NI.GetId(),TPair<TIntV,TFltV>(TIntV(CurrI.GetOutDeg()),TFltV(CurrI.GetOutDeg())));
    }
  }

  int64 NCnt = 0;
  TIntV NIds;
  
  // For each node in InNet get its id
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    NIds.Add(NI.GetId());
  }

// This code is where computational weight falls, so this is to be parallelized
#pragma omp parallel for schedule(dynamic)
  // Preprocess all nodes in InNet
  for (int64 i = 0; i < NIds.Len(); i++) {
    // NCnt is mostly to be ignored as it only is used to display how much work has been done
    PreprocessNode(InNet, ParamP, ParamQ, InNet->GetNI(NIds[i]), NCnt, Verbose);
  }
  if(Verbose){ printf("\n"); }
}

int64 PredictMemoryRequirements(PWNet& InNet) {
  int64 MemNeeded = 0;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    for (int64 i = 0; i < NI.GetOutDeg(); i++) {
      TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
      MemNeeded += CurrI.GetOutDeg()*(sizeof(TInt) + sizeof(TFlt));
    }
  }
  return MemNeeded;
}

//Simulates a random walk
void SimulateWalk(PWNet& InNet, int64 StartNId, const int& WalkLen, TRnd& Rnd, TIntV& WalkV) {
  WalkV.Add(StartNId);
  // If length of walk is one or node is isolated return a walk with only one node
  if (WalkLen == 1) { return; }
  if (InNet->GetNI(StartNId).GetOutDeg() == 0) { return; }

  // Adds next node completely randomly
  // This is why we repeat the walk r times
  WalkV.Add(InNet->GetNI(StartNId).GetNbrNId(Rnd.GetUniDevInt(InNet->GetNI(StartNId).GetOutDeg())));

  // For full rest of walk
  while (WalkV.Len() < WalkLen) {
    // Final element of vector
    int64 Dst = WalkV.Last();
    
    // Penultimate element of vector
    int64 Src = WalkV.LastLast();

    if (InNet->GetNI(Dst).GetOutDeg() == 0) { return; }
    
    // Get random next node (This is probably using that one paper's method)
    int64 Next = AliasDrawInt(InNet->GetNDat(Dst).GetDat(Src),Rnd);
    WalkV.Add(InNet->GetNI(Dst).GetNbrNId(Next));
  }
}
