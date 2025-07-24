#include "stdafx.h"
#include "Snap.h"
#include "biasedrandomwalk.h"

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

int main(int argc, char* argv[]){
  int rank, numprocs;

  
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    
  // We'll create an example network in which to preprocess the walks
  PWNet InNet = PWNet::New();
  PWNet InNet2 = PWNet::New();

  // Having 10 nodes, from 1 to 10
  for(int64 i = 1; i <= 10; i++)
    InNet->AddNode(i);

  for(int64 i = 1; i <= 10; i++)
    InNet2->AddNode(i);

  // Having non-directed edges
  // 1: 2
  // 2: 1, 6
  // 3: 6
  // 4: 8
  // 5: 6, 8
  // 6: 2, 3, 5, 8, 9
  // 7: 8, 10
  // 8: 4, 5, 6, 7
  // 9: 6, 10
  // 10: 7, 9
    
  AddEdgeNotDirected(InNet, 1, 2);
  AddEdgeNotDirected(InNet, 2, 6);
  AddEdgeNotDirected(InNet, 3, 6);
  AddEdgeNotDirected(InNet, 4, 8);
  AddEdgeNotDirected(InNet, 5, 6);
  AddEdgeNotDirected(InNet, 5, 8);
  AddEdgeNotDirected(InNet, 6, 8);
  AddEdgeNotDirected(InNet, 6, 9);
  AddEdgeNotDirected(InNet, 7, 8);
  AddEdgeNotDirected(InNet, 7, 10);
  AddEdgeNotDirected(InNet, 9, 10);

  AddEdgeNotDirected(InNet2, 1, 2);
  AddEdgeNotDirected(InNet2, 2, 6);
  AddEdgeNotDirected(InNet2, 3, 6);
  AddEdgeNotDirected(InNet2, 4, 8);
  AddEdgeNotDirected(InNet2, 5, 6);
  AddEdgeNotDirected(InNet2, 5, 8);
  AddEdgeNotDirected(InNet2, 6, 8);
  AddEdgeNotDirected(InNet2, 6, 9);
  AddEdgeNotDirected(InNet2, 7, 8);
  AddEdgeNotDirected(InNet2, 7, 10);
  AddEdgeNotDirected(InNet2, 9, 10);

  // Distributing the graph in conex components supposes some previous calculations, so i'm not going to do that
  // Firstly distribute InNet 

  // Some way in which I do that RN it's cloned in every process


  // Begin NTTable calculations, each node will do its own rank's+1 node

  // Here we have calculated NTTable for node 6
    

  double ParamP = 0.2;
  double ParamQ = 1.0;

  int Dst = 6; // In my parallelization, node to be calcd (v)
  int Src = 5;

  
  // When joining all results I should just perform SetNData in InNet in process 0 to conjoin all nodes' data as
  // edges are not to be modified (it wouldn't really matter eitherway)
  
  // Supposedly there is no nice way to do this by only using these cpp objects, so it looks like i'll have to rely
  // on using their xml representations

  
  /*
  {
    volatile int i = 0;
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    if(rank==0)printf("PID %d on %s ready for attach\n", getpid(), hostname);
    fflush(stdout);
    if(rank==0){
      while (0 == i)
        sleep(5);
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  */

  // Pretend as if we super nicely distributed work
  PreprocessNodeParallel(InNet, ParamP, ParamQ, InNet->GetNI(rank+1));
  
  int lens[numprocs];
  int displs[numprocs];
  int total;
  char *recv;
  
  TIntIntVFltVPrH hash = InNet->GetNDat(rank+1);
    
  TMOut* stream = new TMOut();

  hash.Save(*stream);

  int len = stream->Len();
  char* bfAddr = stream->GetBfAddr();


  MPI_Reduce(&len, &total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Gather(&len, 1, MPI_INT, lens, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if(rank == 0){
    int displ = 0;
    for(int i = 0; i < numprocs; i++){
      displs[i] = displ;
      displ += lens[i];
    }
    recv = (char*) malloc(sizeof(char)*total);
  }

  
  MPI_Gatherv(bfAddr, len, MPI_BYTE, recv, lens, displs, MPI_BYTE, 0, MPI_COMM_WORLD);

  
  // TODO There are strange problems here
  // The problem seemed to be not allocating recv with malloc (?)
  
  // Memory gather works nicely
  

  MPI_Barrier(MPI_COMM_WORLD);
  
  // This way of gathering works very nicely
  if(rank == 0){
    

    TIntIntVFltVPrH h1 = InNet->GetNDat(1);

    TMIn* in = new TMIn(recv+displs[0], lens[0], false);
    TIntIntVFltVPrH h2 = TIntIntVFltVPrH(*in);
    
    if(h1 == h2){
      printf("Nice job\n");
    } else{
      printf("Oh no\n");
    }

    printf("len: %d\t%d\n", h1.Len(), h2.Len());
    
    // Setting all gathered values
    for(TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      int id = NI.GetId();
      TMIn* inFor = new TMIn(recv+displs[id-1], lens[id-1], false);
      InNet->SetNDat(id, TIntIntVFltVPrH(*inFor));
    }

    // Comparison against prooven result
    PreprocessTransitionProbs(InNet2, ParamP, ParamQ, false);
    
    // I need to find a legitimate way to work on actual ids or something
    for(TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      int id = NI.GetId();
      if(InNet->GetNDat(id) == InNet2->GetNDat(id))
        printf("Node %d OK\n", id);
    }
    
    free(recv);
  }
  

  MPI_Barrier(MPI_COMM_WORLD);

    
  MPI_Finalize();
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
