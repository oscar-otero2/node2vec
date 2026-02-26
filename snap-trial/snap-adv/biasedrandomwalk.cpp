#include "stdafx.h"
#include "Snap.h"
#include "biasedrandomwalk.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <mpi.h>
#include <vector>

// Indicates the process wants to send its results and recv a new piece
#define REQUEST 1
#define DISMISS 2

#define SENDING_BLOCK 10
#define NO_MORE_BLOCKS 20


typedef struct _Storage
{
  bool hasData;

  int edgesLen;
  int* edges;

  int weightsLen;
  float* weights;
  
  int selectedLen;
  int* selected;
} Storage;

typedef struct _ProcStatus
{
  int processingLen; // index == rank
  bool* processing;
  bool* hasReceived;
} ProcStatus;


bool DataLeft(Storage* arr, int len){
  for(int i = 0; i < len; i++){
    if(arr[i].hasData){
      return true;
    }
  }
  return false;
}

int FirstData(Storage* arr, int len){
  for(int i = 0; i < len; i++){
    if(arr[i].hasData){
      return i;
    }
  }
  return -1;
}

bool ProcAvailable(ProcStatus& Status){
  // Proc 0 does not count !!
  for(int i = 1; i < Status.processingLen; i++){
    if(!Status.processing[i]){
      return true;
    }
  }
  return false;
}

bool ProcsFinished(ProcStatus& Status){
  for(int i = 1; i < Status.processingLen; i++){
    if(Status.processing[i]){
      return false;
    }
  }
  return true;
}


// Maybe change i parameter's name?
void StoreChunk(const THash<TInt, TBool>& Selected, const THash<TPair<TInt, TInt>, TFlt>& Edges, Storage* Stored, int index){
  int SelectedLen = Selected.Len();
  Stored[index].selectedLen = SelectedLen;
  int *SelectedBuff = (int *)malloc(SelectedLen * sizeof(int));
  int j = 0;
  for (THash<TInt, TBool>::TIter i = Selected.BegI(); !i.IsEnd(); i.Next()) {
    SelectedBuff[j] = i.GetKey();
    j++;
  }
  Stored[index].selected = SelectedBuff;
  
  int EdgesLen = Edges.Len();
  Stored[index].edgesLen = EdgesLen*2;
  Stored[index].weightsLen = EdgesLen;

  int *EdgesBuff = (int *)malloc(EdgesLen * 2 * sizeof(int));
  float *WeightsBuff = (float *)malloc(EdgesLen * sizeof(float));

  j = 0;
  for (THash<TPair<TInt, TInt>, TFlt>::TIter i = Edges.BegI(); !i.IsEnd(); i.Next()) {
    TPair<TInt, TInt> Edge = i.GetKey();
    int k = j * 2;
    EdgesBuff[k] = Edge.GetVal1();
    EdgesBuff[k + 1] = Edge.GetVal2();
    WeightsBuff[j] = i.GetDat();
    j++;
  }
  Stored[index].edges = EdgesBuff;
  Stored[index].weights = WeightsBuff;

  Stored[index].hasData = true; 
}

void SendChunk(THash<TInt, TBool> Selected,
               THash<TPair<TInt, TInt>, TFlt> Edges, ProcStatus& Status, int Proc);

// Free storage and mark it empty
void SendChunk(Storage& Stored, ProcStatus& Status, int Proc);

bool SendToFirstFree(Storage* Stored, int Blocks, ProcStatus& Status){
  // Get first stored
  int first = FirstData(Stored, Blocks);

  // Do not use rank 0!!
  for(int i = 1; i < Status.processingLen; i++){
    if(!Status.processing[i]){
      SendChunk(Stored[first], Status, i);
      return true;
    }
  }
  return false;
}



// Mini main to test if this is working
void AddEdgeNotDirected(PWNet &InNet, int64 SrcNId, int64 DstNId) {
  InNet->AddEdge(SrcNId, DstNId, 1.0);
  InNet->AddEdge(DstNId, SrcNId, 1.0);
}

int64 AliasDrawInt(TIntVFltVPr &NTTable, TRnd &Rnd);
void PreprocessNodeParallel(PWNet &InNet, const double &ParamP,
                            const double &ParamQ, TWNet::TNodeI NI);
void PreprocessNodeAux(PWNet &InNet, const double &ParamP, const double &ParamQ,
                       TWNet::TNodeI CurrI, TWNet::TNodeI NT);
void GetNodeAlias(TFltV &PTblV, TIntVFltVPr &NTTable);

void PrintH(TIntIntVFltVPrH data) {

  int keyId = data.FFirstKeyId();

  do {
    TIntVFltVPr pr = data.GetDat(data.GetKey(keyId));
    TIntV vect1 = pr.GetVal1();
    TFltV vect2 = pr.GetVal2();
    for (int i = 0; i < vect1.Len() && i < vect2.Len(); i++)
      printf("%d:\t(%d, %f)\n", i, vect1[i], vect2[i]);
  } while (data.FNextKeyId(keyId));
}

THash<TInt, TBool> BFSStep(PWNet &InNet, THash<TInt, TBool> &HM,
                           const THash<TInt, TBool> &LastStep,
                           THash<TInt, TBool> &Selected,
                           THash<TPair<TInt, TInt>, TFlt> &Edges,
                           int ToBeSelectedEdges, int ToBeSelected, int& TotalEdges, bool OnlyOut) {
  // Iterate over HM (probs won't work)

  THash<TInt, TBool> ThisStep;

  // On last It Add all edges, but nodes are not to be selected, just return
  // them
  if (!OnlyOut) {
    // Copy of that one loop
    for (THash<TInt, TBool>::TIter i = Selected.BegI(); !i.IsEnd(); i.Next()) {

      TWNet::TNodeI CurrI = InNet->GetNI(i.GetKey());

      // Out deg first, then in
      for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {
        int n = CurrI.GetNbrNId(j);
        if (!Selected.IsKey(n)) {
          ThisStep.AddKey(n);
          TotalEdges += InNet->GetNI(n).GetDeg();
        }
        // Edges shall be added whether the node was collected before or not
        int v = CurrI.GetId();
        Edges.AddDat(TPair<TInt, TInt>(v, n), InNet->GetEDat(v, n));
      }

      // Now In Deg
      for (int64 j = 0; j < CurrI.GetInDeg(); j++) {
        int n = CurrI.GetInNId(j);

        // We'll do as before
        if (!Selected.IsKey(n)) {
          ThisStep.AddKey(n);
        }

        int v = CurrI.GetId();
        Edges.AddDat(TPair<TInt, TInt>(n, v), InNet->GetEDat(n, v));
      }
    }
    return ThisStep;
  }

  for (THash<TInt, TBool>::TIter i = LastStep.BegI(); !i.IsEnd(); i.Next()) {
    // Get node that has id i
    // Iterate through neighbors
    // Return HM with those that do not appear in Selected

    TWNet::TNodeI CurrI = InNet->GetNI(i.GetKey());

    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {
      int n = CurrI.GetNbrNId(j);

      // Only for new nodes
      if (!Selected.IsKey(n) && HM.IsKey(n)) {
        Selected.AddKey(n);
        HM.DelKey(n);
        TotalEdges += InNet->GetNI(n).GetDeg();
        ThisStep.AddKey(n);

        // Id of source node
        int v = CurrI.GetId();

        Edges.AddDat(TPair<TInt, TInt>(v, n), InNet->GetEDat(v, n));
        // Add these edges too

        // printf("new nodes: %d\n", n);
      }

    if (TotalEdges  >= ToBeSelectedEdges) {
      break;
    }

    }

    
    if (TotalEdges  >= ToBeSelectedEdges) {
      break;
    }
  }
  return ThisStep;
}

void sendHM(TIntIntVFltVPrH &hash, int Proc) {
  // Firstly send total size
  int Len = hash.Len();
  MPI_Send(&Len, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);

  TMOut streamDat;
  int streamDatLen;

  TMOut streamKey;
  int streamKeyLen;

  for (THashKeyDatI<TInt, TIntVFltVPr> i = hash.BegI(); !i.IsEnd(); i++) {
    TIntVFltVPr dat = i.GetDat();
    TInt key = i.GetKey();

    // Data sending
    dat.Save(streamDat);
    streamDatLen = streamDat.Len();

    std::vector<char> bufferDat(streamDatLen);
    memcpy(bufferDat.data(), streamDat.GetBfAddr(), streamDatLen);

    MPI_Send(&streamDatLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
    MPI_Send(bufferDat.data(), streamDatLen, MPI_BYTE, Proc, 0, MPI_COMM_WORLD);
    // Data sent

    // Key sending
    key.Save(streamKey);
    streamKeyLen = streamKey.Len();

    std::vector<char> bufferKey(streamKeyLen);
    memcpy(bufferKey.data(), streamKey.GetBfAddr(), streamKeyLen);

    MPI_Send(&streamKeyLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
    MPI_Send(bufferKey.data(), streamKeyLen, MPI_BYTE, Proc, 0, MPI_COMM_WORLD);
    // Key sent

    streamDat.Clr();
    streamKey.Clr();
  }
}

void recvHM(TIntIntVFltVPrH &hash, int Proc) {
  // First recv total hash len
  int Len;
  MPI_Recv(&Len, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Dat variables
  char *DatBuf;
  int DatLen;
  int StreamDatLen;

  // Key variables
  char *KeyBuf;
  int KeyLen;
  int StreamKeyLen;

  for (int i = 0; i < Len; i++) {

    TIntVFltVPr dat;
    TInt key;

    // Recv data
    MPI_Recv(&StreamDatLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    //DatBuf = (char *)malloc(StreamDatLen);
    DatBuf = new char[StreamDatLen];
    MPI_Recv(DatBuf, StreamDatLen, MPI_BYTE, Proc, 0, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    TMIn inDat(DatBuf, StreamDatLen, true);
    dat.Load(inDat);
    // free(DatBuf); NOT NEEDED
    //  Recv data done

    // Recv key
    MPI_Recv(&StreamKeyLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    //KeyBuf = (char *)malloc(StreamKeyLen);
    KeyBuf = new char[StreamKeyLen];
    MPI_Recv(KeyBuf, StreamKeyLen, MPI_BYTE, Proc, 0, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    TMIn inKey(KeyBuf, StreamKeyLen, true);
    key.Load(inKey);
    // free(KeyBuf); // NOT NEEDED
    //  Recv key done

    hash.AddDat(key, dat);
  }
}

// Send graph pieces to different processes
// TODO a little bit of memory management
void SendChunk(THash<TInt, TBool> Selected,
               THash<TPair<TInt, TInt>, TFlt> Edges, ProcStatus& Status, int Proc) {
  
  Storage Stored;

  // Hacky way of storing in the only Stored available 
  StoreChunk(Selected, Edges, &Stored, 0);

  SendChunk(Stored, Status, Proc);
}

void SendChunk(Storage& Stored, ProcStatus& Status, int Proc) {
    
  // Send selected
  Stored.hasData = false;
  Status.processing[Proc] = true;
  Status.hasReceived[Proc] = true;

  MPI_Send(&(Stored.selectedLen), 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(Stored.selected, Stored.selectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);


  // Send edges
  MPI_Send(&(Stored.edgesLen), 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(Stored.edges, Stored.edgesLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  
  // Send buffs
  MPI_Send(&(Stored.weightsLen), 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(Stored.weights, Stored.weightsLen, MPI_FLOAT, Proc, 0, MPI_COMM_WORLD);


  free(Stored.selected);
  free(Stored.edges);
  free(Stored.weights);
}


void SendResult(PWNet &ProcNet, int SelectedLen, int *SelectedBuff,
                int SelfProc, int Proc);
// Shall join these to work every way?
// Mirror function to SendChunk. Will receive chunks of graph. Probs returns a
// PWNet or TWNet
// TODO a little bit of memory management
void RecvChunk(double ParamP,
                double ParamQ, int SelfProc, int Proc) { // Proc is rank 0 in this case

  int SelectedLen;
  // Recv SelectedLen
  MPI_Recv(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Create all selected's buffer

  int *SelectedBuff = (int *)malloc(SelectedLen * sizeof(int));
  MPI_Recv(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);

  // Recv EdgesLen
  int EdgesLen;
  MPI_Recv(&EdgesLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Recv EdgesBuff
  // int EdgesBuff[EdgesLen*2];
  int *EdgesBuff = (int *)malloc(EdgesLen * sizeof(int));
  MPI_Recv(EdgesBuff, EdgesLen, MPI_INT, Proc, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);


  int WeightsLen;
  MPI_Recv(&WeightsLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  // Recv WeightsBuff
  // float WeightsBuff[EdgesLen];
  float *WeightsBuff = (float *)malloc(WeightsLen * sizeof(float));
  MPI_Recv(WeightsBuff, WeightsLen, MPI_FLOAT, Proc, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);

  // Here we should reform the graph so that the return type is the actual net
  // to be processed and some way of knowing which of the nodes shall be used
  // for processing later

  // Create this network
  PWNet ProcNet = PWNet::New();

  // DANGER
  for (int i = 0; i < EdgesLen / 2; i++) {
    int node1, node2;
    node1 = EdgesBuff[i * 2];
    node2 = EdgesBuff[i * 2 + 1];

    if (!ProcNet->IsNode(node1)) {
      ProcNet->AddNode(node1);
    }
    if (!ProcNet->IsNode(node2)) {
      ProcNet->AddNode(node2);
    }

    ProcNet->AddEdge(node1, node2, WeightsBuff[i]);
  }
  // Return Selected in some incredible way
  
  clock_t begin = clock();
    double begin_nat = omp_get_wtime();



  for (int i = 0; i < SelectedLen; i++) {
    PreprocessNodeParallel(ProcNet, ParamP, ParamQ,
                           ProcNet->GetNI((SelectedBuff)[i]));
  }

    clock_t end = clock();
    double end_nat = omp_get_wtime();

    double _time = double(end - begin) / CLOCKS_PER_SEC;
    double _time_nat = end_nat - begin_nat;

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  printf("<proc rank=\"%d\" process=\"%f\" natural=\"%f\" />\n", rank, _time, _time_nat);


  free(EdgesBuff);
  free(WeightsBuff);

  SendResult(ProcNet, SelectedLen, SelectedBuff, SelfProc, 0);
  free(SelectedBuff);
}

void SendResult(PWNet &ProcNet, int SelectedLen, int *SelectedBuff,
                int SelfProc, int Proc) {

  MPI_Send(&SelfProc, 1, MPI_INT, Proc, REQUEST, MPI_COMM_WORLD);
  // int Lens[SelectedLen];
  // int* Lens = (int*) malloc(SelectedLen*sizeof(int));

  // std::vector<std::vector<char>> buffers(SelectedLen);
  std::vector<int> Lens(SelectedLen);

  // Send data that will be needed
  MPI_Send(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  MPI_Send(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD);
  for (int i = 0; i < SelectedLen; ++i) {
    // TMOut stream;
    TIntIntVFltVPrH hash = ProcNet->GetNDat(SelectedBuff[i]);
    sendHM(hash, Proc);
  }

}

// DO NOT USE
int RecvResult(PWNet &InNet, ProcStatus& Status, int Proc);
int RecvResult(PWNet &InNet, ProcStatus& Status) {
  int Proc;
  MPI_Status status;
  MPI_Recv(&Proc, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &status);
  
  return RecvResult(InNet, Status, Proc);
}
int RecvResult(PWNet &InNet, ProcStatus& Status, int Proc) {
  
  Status.processing[Proc] = false;

  int SelectedLen;

  MPI_Status status;
  MPI_Recv(&SelectedLen, 1, MPI_INT, Proc, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);


  int *SelectedBuff = (int *)malloc(SelectedLen * sizeof(int));
  int *Lens = (int *)malloc(SelectedLen * sizeof(int));

  MPI_Recv(SelectedBuff, SelectedLen, MPI_INT, Proc, 0, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);


  // Allocate and receive each buffer separately
  for (int i = 0; i < SelectedLen; i++) {

    THash<TInt, TPair<TVec<TInt, int>, TVec<TFlt, int> > > hash;

    char *Buf;
    int Len;

    recvHM(hash, Proc);

    InNet->SetNDat(SelectedBuff[i], hash);
  }

  free(SelectedBuff);
  free(Lens);

  printf("Finished %d\n", Proc);
  fflush(stdout);

  return Proc;
}

// Distribution of graph between procs
// GIVES ERRORS WITH 1 RANK ONLY
void DistributeGraph(PWNet &InNet, int NumProcs, int Blocks, Storage* Stored, ProcStatus& Status,
                     THash<TInt, TBool> &SelectedRank0, PWNet &ProcNet0) {

  int NumNodes = InNet->GetNodes();
  int NumEdges = InNet->GetEdges();
  // NUMPROCS-1 BECAUSE WE WON'T USE RANK 0 FOR PROCESSING (WE SHOULD)

  /*
  if(NumProcs == 1){
    for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      PreprocessNodeParallel(InNet, 1.0, 1.0, InNet->GetNI(NI.GetId()));
    }
  }
  */

  // Distribute in more blocks than procs
  int ToBeSelected = (NumNodes / (Blocks)) + 1; // Por si van de menos
  int ToBeSelectedEdges = (NumEdges / (Blocks)) + 1;
  printf("\nEdges: %d\nNodes: %d\nBlocks: %d\nToBeSelectedEdges: %d\n", NumEdges,
         NumNodes, Blocks, ToBeSelectedEdges);

  // Create HM of all nodes
  THash<TInt, TBool> HM;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    HM.AddKey(NI.GetId());
  }

  // GetRndKeyId (TRnd &Rnd) const

  TRnd rand = TRnd();
  
  // Distribute the graph in some qty of blocks
  for (int i = 0; i < Blocks; i++) { // Get key TODO (from random node in HM)
    // This sampling is faulty
    // TWNet::TNodeI Rand = InNet->GetRndNI(rand);
    int TotalEdges = 0;

    THash<TInt, TBool> Selected;
    THash<TInt, TBool> LastStep;

    THash<TPair<TInt, TInt>, TFlt> Edges;

    // Now this doesn't break but is very strange
    if (HM.Empty()) {
      // TODO: This can be breaking things
      
      //SendChunk(Selected, Edges, i);
      // Horrifying once again
      continue;
    }

    int Rand = HM.GetKey(HM.GetRndKeyId(rand));
    Selected.AddKey(Rand);
    TotalEdges += InNet->GetNI(Rand).GetDeg();
    LastStep.AddKey(Rand);
    HM.DelKey(Rand);

    // Only out edges until last iteration, when we'll get in and out ones

    //while (Selected.Len() < ToBeSelected && !HM.Empty()) {
    //  Make sure that all nodes are being visited still
    //  Usual condition + safeguard to always finish sending
    
    // enough -> TotalEdges/2 > ToBeSelectedEdges || Selected.Len() > ToBeSelected
    // not enoudh -> TotalEdges/2 < ToBeSelectedEdges && Selected.Len() < ToBeSelected
    //while (
    //  (((TotalEdges / 2) < ToBeSelectedEdges && Selected.Len() < ToBeSelected) && !HM.Empty()) 
    //  || (i == NumProcs-1 && !HM.Empty())
    //  ){
    while ((TotalEdges / 2 < ToBeSelectedEdges && !HM.Empty()) || (i == Blocks-1 && !HM.Empty())){

      LastStep = BFSStep(InNet, HM, LastStep, Selected, Edges,
                         ToBeSelectedEdges - Edges.Len(), ToBeSelected - Selected.Len(), TotalEdges, true);
      if (LastStep.Empty()) {

        // This method is scarily biased, but random sampling of the hash is
        // faulty as it returns nonexistant ids over and over (0)
        
        int NewKey = HM.GetKey(HM.GetRndKeyId(rand));
        Selected.AddKey(NewKey);
        TotalEdges += InNet->GetNI(NewKey).GetDeg();
        HM.DelKey(NewKey);
        LastStep.AddKey(NewKey);

      } else {
      for (THash<TInt, TBool>::TIter j = LastStep.BegI(); !j.IsEnd();
           j.Next()) {
          // Already done inside searchfunction
          /*
        Selected.AddKey(j.GetKey());
        HM.DelKey(j.GetKey());
        */
      }
      }
    }

    printf("\nRank: %d -> (%d nodes) (%d edges) (%d estimated) ", i, Selected.Len(),
           Edges.Len(), TotalEdges/2);

    THash<TInt, TBool> Additional =
        BFSStep(InNet, HM, Selected, Selected, Edges, 0, 0, TotalEdges, false);

    printf("After addit (%d edges)\n", Edges.Len());
    fflush(stdout);

    
    // First of all, if any process is available just send data to it and prepare next chunk
    if(ProcAvailable(Status)){
      Storage _Stored;
      // Another hacky storage
      StoreChunk(Selected, Edges, &_Stored, 0);
      // Hacky way of sending
      SendToFirstFree(&_Stored, 1, Status);
      printf("Could send!");
      fflush(stdout);

    } else {

      // Check if any process is requesting more blocks
      int flag;
      MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
      
      // If any process is requesting blocks
      if(flag){
        // Recv process id
        
        // Receive results of the process and send new data to it
        // This automatically gets data into the net
        int Proc = RecvResult(InNet, Status);


        int Response = SENDING_BLOCK;
        MPI_Send(&Response, 1, MPI_INT, Proc, REQUEST, MPI_COMM_WORLD);

        // Send remaining chunk
        SendChunk(Selected, Edges, Status, Proc);
      
        // Store buffers to be sent
      } else {

        StoreChunk(Selected, Edges, Stored, i);

      }
    }

  }
  printf("Distrib done?");
  fflush(stdout);
}

void PreprocessNodeParallel(PWNet &InNet, const double &ParamP,
                            const double &ParamQ, TWNet::TNodeI NI) {

  // TODO !!!
  // Maybe initializing the graph would be great I guess. This is probably
  // helping a lot with the overhead, So I should probably just be creating the
  // data for those nodes that have been assigned the v

  InNet->SetNDat(NI.GetId(), TIntIntVFltVPrH());

  // Allocate the necessary space in the hashtable (only for the node to be
  // calc'd)
  // With this horrifying fix that gets full deg, the code now works on cluster
  // environment wtf
  for (int64 i = 0; i < NI.GetDeg();
       i++) { // allocating space in advance to avoid issues with multithreading
    TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
    // Get node data (what is it)
    // Add to it (as hashtable) (its id, a pair of an int vector and a float
    // vector of the size of the out degree of the node)
    NI.GetDat().AddDat(
        CurrI.GetId(),
        TPair<TIntV, TFltV>(TIntV(NI.GetOutDeg()), TFltV(NI.GetOutDeg())));
  }

  for (int64 i = 0; i < NI.GetInDeg(); i++) {
    // TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));      //for each node
    // t
    //  As NI is the current node to be calculated and NT are its neighbors
    PreprocessNodeAux(InNet, ParamP, ParamQ, NI, InNet->GetNI(NI.GetInNId(i)));
  }
}

void PreprocessNodeAux(PWNet &InNet, const double &ParamP, const double &ParamQ,
                       TWNet::TNodeI CurrI, TWNet::TNodeI NT) {

  THash<TInt, TBool> NbrH; // Neighbors of t
  for (int64 i = 0; i < NT.GetOutDeg(); i++) {
    NbrH.AddKey(NT.GetNbrNId(i));
  }

  // For considered V (CurrI)
  double Psum = 0;
  TFltV PTable; // Probability distribution table

  for (int64 j = 0; j < CurrI.GetOutDeg(); j++) { // for each node x
    int64 FId = CurrI.GetNbrNId(j);
    TFlt Weight;

    // If <something> ignore x node
    // All of the values that appear after this are directly explained in the
    // paper: Section 3.2.2
    if (!(InNet->GetEDat(CurrI.GetId(), FId, Weight))) {
      continue;
    }
    if (FId == NT.GetId()) {
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
  // Normalizing table
  for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {
    PTable[j] /= Psum;
  }
  // Main result of these calculations is the PTable, unique for each node, that
  // requires up to 2 distance neighbours for each of these Only NTTAble is
  // being modified.
  GetNodeAlias(PTable, CurrI.GetDat().GetDat(NT.GetId()));
}

// Preprocess alias sampling method
//  No info from outside of these considered neighbours is being passed arround
void GetNodeAlias(TFltV &PTblV, TIntVFltVPr &NTTable) {
  int64 N = PTblV.Len();

  // These are the vectors stored for every node
  TIntV &KTbl = NTTable.Val1;
  TFltV &UTbl = NTTable.Val2;

  // Init them to 0
  for (int64 i = 0; i < N; i++) {
    KTbl[i] = 0;
    UTbl[i] = 0;
  }

  // UnderV has indices for those neighbours where
  // Probability * num of neighbours < 1
  // The opposite is true for OverV
  TIntV UnderV;
  TIntV OverV;

  // For each neighbour that we evaluated
  for (int64 i = 0; i < N; i++) {
    // Float in node = probability * Num of neighbours
    UTbl[i] = PTblV[i] * N;
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

  while (UnderV.Len() > 0) {
    int64 curr = UnderV.Last();
    UnderV.DelLast();
    UTbl[curr] = 1;
  }
  while (OverV.Len() > 0) {
    int64 curr = OverV.Last();
    OverV.DelLast();
    UTbl[curr] = 1;
  }
}

// Get random element using alias sampling method
int64 AliasDrawInt(TIntVFltVPr &NTTable, TRnd &Rnd) {
  int64 N = NTTable.GetVal1().Len();
  TInt X = static_cast<int64>(Rnd.GetUniDev() * N);
  double Y = Rnd.GetUniDev();
  return Y < NTTable.GetVal2()[X] ? X : NTTable.GetVal1()[X];
}

// Process each node
// This is an interesting function but I don't think I need to explain it all
// Because its end goal is to give us the probabilities of taking each edge
// They are given in the node itself(?)
void PreprocessNode(PWNet &InNet, const double &ParamP, const double &ParamQ,
                    TWNet::TNodeI NI, int64 &NCnt, const bool &Verbose) {
  if (Verbose && NCnt % 100 == 0) {
    printf("\rPreprocessing progress: %.2lf%% ",
           (double)NCnt * 100 / (double)(InNet->GetNodes()));
    fflush(stdout);
  }
  // for node t
  THash<TInt, TBool> NbrH; // Neighbors of t
  for (int64 i = 0; i < NI.GetOutDeg(); i++) {
    NbrH.AddKey(NI.GetNbrNId(i));
  }

  // For each neighbour
  for (int64 i = 0; i < NI.GetOutDeg(); i++) {
    TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i)); // for each node v
    double Psum = 0;
    TFltV PTable; // Probability distribution table

    // For each neighbour's neighbours
    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) { // for each node x
      int64 FId = CurrI.GetNbrNId(j);
      TFlt Weight;

      // If <something> ignore x node
      // All of the values that appear after this are directly explained in the
      // paper: Section 3.2.2
      if (!(InNet->GetEDat(CurrI.GetId(), FId, Weight))) {
        continue;
      }
      if (FId == NI.GetId()) {
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
    // Normalizing table
    for (int64 j = 0; j < CurrI.GetOutDeg(); j++) {
      PTable[j] /= Psum;
    }
    // Main result of these calculations is the PTable, unique for each node,
    // that requires up to 2 distance neighbours for each of these Only NTTAble
    // is being modified.
    GetNodeAlias(PTable, CurrI.GetDat().GetDat(NI.GetId()));
  }
  NCnt++;
}

// Preprocess transition probabilities for each path t->v->x

///////////////////////////
// TODO: Parallelization //
///////////////////////////

void PreprocessTransitionProbs(PWNet &InNet, const double &ParamP,
                               const double &ParamQ, const int& Blocks, const bool &verbose) {

  int rank, numprocs;

  /*
  MPI_Init(&argc, &argv);
  */
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    // For each node in InNet
    printf("\n\nBLOCKS -> %d\n\n", Blocks);
    for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      InNet->SetNDat(NI.GetId(), TIntIntVFltVPrH());
    }

    // For each node in InNet
    for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      // For all neighbours
      for (int64 i = 0; i < NI.GetOutDeg();
           i++) { // allocating space in advance to avoid issues with
                  // multithreading
        TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
        // Get node data (what is it)
        // Add to it (as hashtable) (its id, a pair of an int vector and a float
        // vector of the size of the out degree of the node)
        CurrI.GetDat().AddDat(NI.GetId(),
                              TPair<TIntV, TFltV>(TIntV(CurrI.GetOutDeg()),
                                                  TFltV(CurrI.GetOutDeg())));
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
  int *SelectedBuff;

  PWNet ProcNet;
  THash<TInt, TBool> SelectedRank0;
  if (rank == 0) {

    int BlocksSent = 0;
    // Blocks of storage
    // Each will be individually alloc'd if needed
    Storage* Stored = (Storage*)malloc(sizeof(Storage)*Blocks);
    for(int i = 0; i < Blocks; i++){
      Stored[i].hasData = false;
    }

    ProcStatus Status;
    Status.processingLen = numprocs;
    Status.processing = (bool*)malloc(sizeof(bool)*numprocs);
    Status.hasReceived = (bool*)malloc(sizeof(bool)*numprocs);
    for(int i = 0; i < numprocs; i++){
      Status.processing[i] = false;
      Status.hasReceived[i] = false;
    }

    clock_t begin = clock();
    double begin_nat = omp_get_wtime();

    DistributeGraph(InNet, numprocs, Blocks, Stored, Status, SelectedRank0, ProcNet);

    clock_t end = clock();
    double end_nat = omp_get_wtime();

    double _time = double(end - begin) / CLOCKS_PER_SEC;
    double _time_nat = end_nat - begin_nat;

    printf("<distribution process=\"%f\" natural=\"%f\" />", _time, _time_nat);

    // Start Recv for all remaining Procs
    while(DataLeft(Stored, Blocks)){
      
      int Proc = RecvResult(InNet, Status);
      
      int Response = SENDING_BLOCK;
      MPI_Send(&Response, 1, MPI_INT, Proc, REQUEST, MPI_COMM_WORLD);

      // Send remaining chunk
      int first = FirstData(Stored, Blocks);
      SendChunk(Stored[first], Status, Proc);

    }
    // Dismiss processes
    for(int i = 1; i < numprocs; i++){
      if(!Status.hasReceived[i]){
        int Dismiss = DISMISS;
        MPI_Send(&Dismiss, 1, MPI_INT, i, REQUEST, MPI_COMM_WORLD);
      }
    }

    // Until all procs have sent
    while(!ProcsFinished(Status)){

      int Proc = RecvResult(InNet, Status);
      
      int Response = NO_MORE_BLOCKS;
      MPI_Send(&Response, 1, MPI_INT, Proc, REQUEST, MPI_COMM_WORLD);
    }

  } else {
    
    // TODO, VERY BIG TODO
    // THIS WILL PROBABLY GET STUCK AS IT HAS TO BE SENT AT LEAST ONE CHUNK

    // Be careful and wait for a dismiss too
    // Probe message on request -> Dismiss
    // Probe message on tag 0 -> SendChunk
    int dismiss = 0;
    int recv = 0;


    while(!dismiss && !recv){
      // Request tag
      MPI_Iprobe(0, REQUEST, MPI_COMM_WORLD, &dismiss, MPI_STATUS_IGNORE);
    
      // Message tag
      MPI_Iprobe(0, 0, MPI_COMM_WORLD, &recv, MPI_STATUS_IGNORE);
    }
    
    if(recv){
      //Already sends results
      RecvChunk(ParamP, ParamQ, rank, 0);

    
      // Send result, which also send the request first
      //SendResult(ProcNet, SelectedLen, SelectedBuff, rank, 0);
      //free(SelectedBuff);

      // Recv request's result
      int isSending;
      MPI_Recv(&isSending, 1, MPI_INT, 0, REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
      while(isSending == SENDING_BLOCK){
        // Already sends results
        RecvChunk(ParamP, ParamQ, rank, 0);
        MPI_Recv(&isSending, 1, MPI_INT, 0, REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }

    // We shall rejoin data once again here
    // But we still need to have Selected nodes in mind to do so
  }

  // Gathering seems functional
  if (rank == 0) {
    // Proc 0 isn't sending

    for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
      int id = NI.GetId();

      TIntIntVFltVPrH data = InNet->GetNDat(id);
      if (data.Empty())
        printf("Something wrong\n"); // Better to throw some error

      for (THash<TInt, TIntVFltVPr>::TIter i = data.BegI(); !i.IsEnd();
           i.Next()) {
        TIntVFltVPr vect = data.GetDat(i.GetKey());
        TIntV intv = vect.GetVal1();
        TFltV fltv = vect.GetVal2();
      }
    }

  } else {
  }
}
/*
void PreprocessTransitionProbs(PWNet& InNet, const double& ParamP, const double&
ParamQ, const bool& Verbose) {
  // For each node in InNet
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    InNet->SetNDat(NI.GetId(),TIntIntVFltVPrH());
  }

  // For each node in InNet
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    // For all neighbours
    for (int64 i = 0; i < NI.GetOutDeg(); i++) {                    //allocating
space in advance to avoid issues with multithreading TWNet::TNodeI CurrI =
InNet->GetNI(NI.GetNbrNId(i));
      // Get node data (what is it)
      // Add to it (as hashtable) (its id, a pair of an int vector and a float
vector of the size of the out degree of the node)
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
    // NCnt is mostly to be ignored as it only is used to display how much work
has been done PreprocessNode(InNet, ParamP, ParamQ, InNet->GetNI(NIds[i]), NCnt,
Verbose);
  }
  if(Verbose){ printf("\n"); }
}
*/

int64 PredictMemoryRequirements(PWNet &InNet) {
  int64 MemNeeded = 0;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    for (int64 i = 0; i < NI.GetOutDeg(); i++) {
      TWNet::TNodeI CurrI = InNet->GetNI(NI.GetNbrNId(i));
      MemNeeded += CurrI.GetOutDeg() * (sizeof(TInt) + sizeof(TFlt));
    }
  }
  return MemNeeded;
}

// Simulates a random walk
void SimulateWalk(PWNet &InNet, int64 StartNId, const int &WalkLen, TRnd &Rnd,
                  TIntV &WalkV) {
  WalkV.Add(StartNId);
  // If length of walk is one or node is isolated return a walk with only one
  // node
  if (WalkLen == 1) {
    return;
  }
  if (InNet->GetNI(StartNId).GetOutDeg() == 0) {
    return;
  }

  // Adds next node completely randomly
  // This is why we repeat the walk r times
  WalkV.Add(InNet->GetNI(StartNId).GetNbrNId(
      Rnd.GetUniDevInt(InNet->GetNI(StartNId).GetOutDeg())));

  // For full rest of walk
  while (WalkV.Len() < WalkLen) {
    // Final element of vector
    int64 Dst = WalkV.Last();

    // Penultimate element of vector
    int64 Src = WalkV.LastLast();

    if (InNet->GetNI(Dst).GetOutDeg() == 0) {
      return;
    }

    // Get random next node (This is probably using that one paper's method)
    int64 Next = AliasDrawInt(InNet->GetNDat(Dst).GetDat(Src), Rnd);
    WalkV.Add(InNet->GetNI(Dst).GetNbrNId(Next));
  }
}