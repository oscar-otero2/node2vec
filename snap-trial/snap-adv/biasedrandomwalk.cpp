#include "stdafx.h"
#include "Snap.h"
#include "biasedrandomwalk.h"

#include <cstdio>
#include <mpi.h>


//Mini main to test if this is working
void AddEdgeNotDirected(PWNet& InNet, int64 SrcNId, int64 DstNId){
    InNet->AddEdge(SrcNId, DstNId, 1.0);
    InNet->AddEdge(DstNId, SrcNId, 1.0);
}

int64 AliasDrawInt(TIntVFltVPr& NTTable, TRnd& Rnd);
void PreprocessNodeParallel (PWNet& InNet, const double& ParamP, const double& ParamQ, TWNet::TNodeI NI);
void PreprocessNodeAux (PWNet& InNet, const double& ParamP, const double& ParamQ, TWNet::TNodeI CurrI, TWNet::TNodeI NT);
void GetNodeAlias(TFltV& PTblV, TIntVFltVPr& NTTable);

void PrintH(TIntIntVFltVPrH data){

  int keyId = data.FFirstKeyId();

  do{
    TIntVFltVPr pr = data.GetDat(data.GetKey(keyId));
    TIntV vect1 = pr.GetVal1();
    TFltV vect2 = pr.GetVal2();
    for(int i = 0; i < vect1.Len() && i < vect2.Len(); i++)
      printf("%d:\t(%d, %f)\n", i, vect1[i], vect2[i]);
  } while (data.FNextKeyId(keyId));

}

THash<TInt, TBool> BFSStep(PWNet& InNet, THash<TInt, TBool> HM, THash<TInt, TBool> LastStep, THash<TInt, TBool>& Selected, THash<TPair<TInt, TInt>, TFlt>& Edges, bool OnlyOut){
  // Iterate over HM (probs won't work)
  
  THash<TInt, TBool> ThisStep;

  // On last It Add all edges, but nodes are not to be selected, just return them
  if(!OnlyOut){
    // Copy of that one loop
    for(THash<TInt, TBool>::TIter i = LastStep.BegI(); !i.IsEnd(); i.Next()){

      TWNet::TNodeI CurrI = InNet->GetNI(i.GetKey());

      // Out deg first, then in
      for(int64 j = 0; j < CurrI.GetOutDeg(); j++){
        int n = CurrI.GetNbrNId(j);
        if(!Selected.IsKey(n)){
          ThisStep.AddKey(n);
        }
        // Edges shall be added whether the node was collected before or not
        int v = CurrI.GetId();
        Edges.AddDat(TPair<TInt, TInt>(v, n), InNet->GetEDat(v, n));
      }
        
      // Now In Deg
      for(int64 j = 0; j < CurrI.GetInDeg(); j++){
        int n = CurrI.GetInNId(j);
          
        // We'll do as before
        if(!Selected.IsKey(n)){ 
          ThisStep.AddKey(n);
        }
        
        int v = CurrI.GetId();
        Edges.AddDat(TPair<TInt, TInt>(n, v), InNet->GetEDat(n, v));
      }
    }
    return ThisStep;
  }


  for(THash<TInt, TBool>::TIter i = LastStep.BegI(); !i.IsEnd(); i.Next()){
    // Get node that has id i
    // Iterate through neighbors
    // Return HM with those that do not appear in Selected

    TWNet::TNodeI CurrI = InNet->GetNI(i.GetKey());
    
    for(int64 j = 0; j < CurrI.GetOutDeg(); j++){
      int n = CurrI.GetNbrNId(j);
      
      // Only for new nodes
      if(!Selected.IsKey(n) && HM.IsKey(n)){
        Selected.AddKey(n);
        ThisStep.AddKey(n);
      
        // Id of source node
        int v = CurrI.GetId();

        Edges.AddDat(TPair<TInt, TInt>(v, n), InNet->GetEDat(v, n));
        // Add these edges too

        // printf("new nodes: %d\n", n);
      }
    }
  }
  return ThisStep;
}

// Send graph pieces to different processes
// TODO a little bit of memory management
void SendChunk(THash<TInt, TBool> Selected, THash<TPair<TInt, TInt>, TFlt> Edges, int Proc){
  // We should deserialize and we have to send both all edges and nodes to be processed.
  // Another way of doing so is to process everything (not cost efficient probs), and on return to
  // Rank 0, reorder results. We shall do fst
  
  // Firstly, Selected length
  
  int SelectedLen = Selected.Len();
  MPI_Send(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  
  // Recv will create buffer to receive all nodes
  
  
  int SelectedBuff[SelectedLen];
  // Nasty loop
  int j = 0;
  for(THash<TInt, TBool>::TIter i = Selected.BegI(); !i.IsEnd(); i.Next()){
    SelectedBuff[j] = i.GetKey();
    j++;
  }
  MPI_Send(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);

  // Nodes done. Now for edges ->
  
  int EdgesLen = Edges.Len();
  MPI_Send(&EdgesLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  
  // Recv will create a buffer for all edges
  // We'll use two buffs, one for edge nodes, and the other for edge weights
  
  int EdgesBuff[EdgesLen*2];
  float WeightsBuff[EdgesLen];
  // Nasty loop
  j = 0;
  for(THash<TPair<TInt, TInt>, TFlt>::TIter i = Edges.BegI(); !i.IsEnd(); i.Next()){
    TPair<TInt, TInt> Edge = i.GetKey();
    int k = j*2;
    EdgesBuff[k] = Edge.GetVal1();
    EdgesBuff[k+1] = Edge.GetVal2();
    WeightsBuff[j] = i.GetDat();
    j++;
  }
  MPI_Send(EdgesBuff, EdgesLen*2, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(WeightsBuff, EdgesLen, MPI_FLOAT, Proc, 0, MPI_COMM_WORLD);

  
}

// Shall join these to work every way?
// Mirror function to SendChunk. Will receive chunks of graph. Probs returns a PWNet or TWNet
// TODO a little bit of memory management
PWNet RecvChunk(int* SelectedLen, int** SelectedBuff, double ParamP, double ParamQ, int Proc){ // Proc is rank 0 in this case
  
  // Recv SelectedLen
  MPI_Recv(SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  
  // Create all selected's buffer
  
  *SelectedBuff = (int*) malloc(*SelectedLen*sizeof(int));
  MPI_Recv(*SelectedBuff, *SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  for(int i = 0; i < *SelectedLen; i++)
    printf("process %d\n", (*SelectedBuff)[i]);

  // Recv EdgesLen
  int EdgesLen;
  MPI_Recv(&EdgesLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Recv EdgesBuff
  int EdgesBuff[EdgesLen*2];
  MPI_Recv(EdgesBuff, EdgesLen*2, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Recv WeightsBuff
  int WeightsBuff[EdgesLen];
  MPI_Recv(WeightsBuff, EdgesLen, MPI_FLOAT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);


  // Here we should reform the graph so that the return type is the actual net to be processed
  // and some way of knowing which of the nodes shall be used for processing later
  
  printf("Could receive\n");

  // Create this network
  PWNet ProcNet = PWNet::New();

  for(int i = 0; i < EdgesLen; i++){
    int node1, node2;
    node1 = EdgesBuff[i*2];
    node2 = EdgesBuff[i*2+1];

    if(!ProcNet->IsNode(node1)){ ProcNet->AddNode(node1); }
    if(!ProcNet->IsNode(node2)){ ProcNet->AddNode(node2); }

    ProcNet->AddEdge(node1, node2, WeightsBuff[i]);
  }
  // Return Selected in some incredible way
 
  for(int i = 0; i < *SelectedLen; i++){
    PreprocessNodeParallel(ProcNet, ParamP, ParamQ, ProcNet->GetNI((*SelectedBuff)[i]));
  }
  
  return ProcNet;
}

void SendResult(PWNet& ProcNet, int SelectedLen, int* SelectedBuff, int SelfProc, int Proc){

  int Lens[SelectedLen];
  char** SendBuff = (char**) malloc(SelectedLen * sizeof(char*));
  
  for(int i = 0; i < SelectedLen; i++){

    TIntIntVFltVPrH hash = ProcNet->GetNDat(SelectedBuff[i]);
    TMOut* stream = new TMOut();
    hash.Save(*stream);

    Lens[i] = stream->Len();
    SendBuff[i] = stream->GetBfAddr();
  }
  
  MPI_Send(&SelfProc, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(Lens, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);

  for(int i = 0; i < SelectedLen; i++){
    MPI_Send(SendBuff[i], Lens[i], MPI_BYTE, Proc, 0, MPI_COMM_WORLD);
  }

  free(SendBuff);

}

void RecvResult(PWNet& InNet){
  
  int Proc;
  int SelectedLen;

  // TODO
  // CAN GET PROC ID FROM MPI STATUS
  MPI_Recv(&Proc, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  MPI_Recv(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  
  int SelectedBuff[SelectedLen];
  int Lens[SelectedLen];
  int Displs[SelectedLen];

  MPI_Recv(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  MPI_Recv(Lens, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
 
  // Setup displs (only needed if only one send later)
  int Displ = 0;
  for(int i = 0; i < SelectedLen; i++){
    Displs[i] = Displ;
    Displ += Lens[i];
  }
  
  // Allocing mem for this send
  char** RecvBuff = (char**) malloc(SelectedLen * sizeof(char*));
  for(int i = 0; i < SelectedLen; i++){
    RecvBuff[i] = (char*) malloc(Lens[i] * sizeof(char));
    //printf("Lens[%d] = %d\n", i, Lens[i]);
  }

  // Maybe change this with only one (1) send and recv
  for(int i = 0; i < SelectedLen; i++){
    MPI_Recv(RecvBuff[i], Lens[i], MPI_BYTE, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  }
  
  printf("Could recv");
  
  // Here re-transform these RecvBuff into hashes and insert them into their respective nodes
  for(int i = 0; i < SelectedLen; i++){
    TMIn* inFor = new TMIn(RecvBuff[i], Lens[i], false);
    InNet->SetNDat(SelectedBuff[i], TIntIntVFltVPrH(*inFor));
  }
  
  for(int i = 0; i < SelectedLen; i++){
    free(RecvBuff[i]);
  }
  free(RecvBuff);
}


// Distribution of graph between procs
// GIVES ERRORS WITH 1 RANK ONLY
void DistributeGraph(PWNet& InNet, int NumProcs){

  int NumNodes = InNet->GetNodes();
  // NUMPROCS-1 BECAUSE WE WON'T USE RANK 0 FOR PROCESSING (WE SHOULD)
  int ToBeSelected = (NumNodes / (NumProcs-1)) + 1; // Por si van de menos

  printf("TO BE SELECTED = %d\n", ToBeSelected);
  // Create HM of all nodes
  THash<TInt, TBool> HM;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    HM.AddKey(NI.GetId());
  }

  //GetRndKeyId (TRnd &Rnd) const 


  TRnd rand = TRnd();
  // TODO Check about sending this data to rank 0. Maybe do it in another way?
  // i = 1 BECAUSE IN THIS PROOF OF CONCEPT WE WON'T BE USING RANK 0 FOR PROCESSING
  // PROBLEMS WITH ONLY 2 PROCESSES TOO
  for(int i = 1; i < NumProcs; i++){ // Get key TODO (from random node in HM)
    // This sampling is faulty
    // TWNet::TNodeI Rand = InNet->GetRndNI(rand);



    // Horible sampling
    int Rand = HM.BegI().GetKey();

    THash<TInt, TBool> Selected;
    THash<TInt, TBool> LastStep;

    THash<TPair<TInt, TInt>, TFlt> Edges;

    // Now this doesn't break but is very strange
    if(HM.Empty()){
      SendChunk(Selected, Edges, i);
      // Horrifying once again
      continue;
    }

    Selected.AddKey(Rand);
    LastStep.AddKey(Rand);
    HM.DelKey(Rand);

    // Only out edges until last iteration, when we'll get in and out ones
    
    while(Selected.Len() < ToBeSelected && !HM.Empty()){
      LastStep = BFSStep(InNet, HM, LastStep, Selected, Edges, true);
      if(LastStep.Empty()){
        
        // This method is scarily biased, but random sampling of the hash is faulty as it returns nonexistant ids over and over (0)
        int NewKey = HM.BegI().GetKey();
        Selected.AddKey(NewKey);
        HM.DelKey(NewKey);

        // Oof, evil gotos and continues
        continue;
      }
      for(THash<TInt, TBool>::TIter i = LastStep.BegI(); !i.IsEnd(); i.Next()){
        Selected.AddKey(i.GetKey());
        HM.DelKey(i.GetKey());
      }
    }
    
    THash<TInt, TBool> Additional = BFSStep(InNet, HM, Selected, Selected, Edges, false);
    
    
    printf("Nodes: \n\n");
    for(THash<TInt, TBool>::TIter i = Selected.BegI(); !i.IsEnd(); i.Next()){
      printf("%d\n", i.GetKey());
    }

    printf("Edges:\n\n");
    for(THash<TPair<TInt, TInt>, TFlt>::TIter i = Edges.BegI(); !i.IsEnd(); i.Next()){
      TPair<TInt, TInt> edge = i.GetKey();
      printf("%d\t%d\t:\t%f\n", edge.GetVal1(), edge.GetVal2(), i.GetDat());
    }
    
    printf("Sending to %d\n", i);
    SendChunk(Selected, Edges, i);
    
  }
}


void PreprocessNodeParallel (PWNet& InNet, const double& ParamP, const double& ParamQ,
 TWNet::TNodeI NI) {
    
    // TODO !!!
    // Maybe initializing the graph would be great I guess. This is probably helping a lot with the overhead,
    // So I should probably just be creating the data for those nodes that have been assigned the v

    InNet->SetNDat(NI.GetId(),TIntIntVFltVPrH());

    // Allocate the necessary space in the hashtable (only for the node to be calc'd)
    for (int64 i = 0; i < NI.GetOutDeg(); i++) {                    //allocating space in advance to avoid issues with multithreading
      TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
      // Get node data (what is it)
      // Add to it (as hashtable) (its id, a pair of an int vector and a float vector of the size of the out degree of the node)
      NI.GetDat().AddDat(CurrI.GetId(),TPair<TIntV,TFltV>(TIntV(NI.GetOutDeg()),TFltV(NI.GetOutDeg())));
    }

  

  for (int64 i = 0; i < NI.GetInDeg(); i++) {
    //TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));      //for each node t
        // As NI is the current node to be calculated and NT are its neighbors
        PreprocessNodeAux(InNet, ParamP, ParamQ, NI, InNet->GetNI(NI.GetInNId(i)));
  }
}

void PreprocessNodeAux (PWNet& InNet, const double& ParamP, const double& ParamQ,
 TWNet::TNodeI CurrI, TWNet::TNodeI NT) {

  THash <TInt, TBool> NbrH;                                    //Neighbors of t
  for (int64 i = 0; i < NT.GetOutDeg(); i++) {
    NbrH.AddKey(NT.GetNbrNId(i));
  } 

    
    // For considered V (CurrI)
    double Psum = 0;
    TFltV PTable;                              //Probability distribution table
    

    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {           //for each node x
      int64 FId = CurrI.GetNbrNId(j);
      TFlt Weight;
      
      // If <something> ignore x node
      // All of the values that appear after this are directly explained in the paper:
      // Section 3.2.2
      if (!(InNet->GetEDat(CurrI.GetId(), FId, Weight))){ continue; }
      if (FId==NT.GetId()) {
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
    GetNodeAlias(PTable,CurrI.GetDat().GetDat(NT.GetId()));
}

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
  int64 N = NTTable.GetVal1().Len();
  TInt X = static_cast<int64>(Rnd.GetUniDev()*N);
  double Y = Rnd.GetUniDev();
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

void PreprocessTransitionProbs(PWNet &InNet, const double &ParamP, const double &ParamQ, const bool &verbose){

  int rank, numprocs;


  /*
  MPI_Init(&argc, &argv);
  */
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);


  if(rank == 0){
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
  }

  // My own process 


  int SelectedLen;
  int* SelectedBuff;
  PWNet ProcNet;
  if(rank == 0){
    DistributeGraph(InNet, numprocs);
  } else {
    // Already Processed
    ProcNet = RecvChunk(&SelectedLen, &SelectedBuff, ParamP, ParamQ, 0);
    printf("rank %d done\n", rank);
    
    
    // We shall rejoin data once again here
    // But we still need to have Selected nodes in mind to do so
  }
  
  // Gathering seems functional
  if(rank == 0){
    // Proc 0 isn't sending
    for(int i = 1; i < numprocs; i++)
      RecvResult(InNet);
    
    printf("Nodes\n\n");
    for(TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      int id = NI.GetId();
      printf("\n%d\n", id);

      TIntIntVFltVPrH data = InNet->GetNDat(id);
      if(data.Empty()) printf("Something wrong\n");

      for(THash<TInt, TIntVFltVPr>::TIter i = data.BegI(); !i.IsEnd(); i.Next()){
        TIntVFltVPr vect = data.GetDat(i.GetKey());
        TIntV intv = vect.GetVal1();
        TFltV fltv = vect.GetVal2();

        for(int j = 0; j < intv.Len(); j++){
          printf("\t(%d  %f)\n", intv[j], fltv[j]);
        }


      }

    }

  } else {
    SendResult(ProcNet, SelectedLen, SelectedBuff, rank, 0);

  }

}
/*
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
*/

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