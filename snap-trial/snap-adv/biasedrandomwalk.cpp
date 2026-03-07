#include "stdafx.h"
#include "Snap.h"
#include "biasedrandomwalk.h"

#include <cstddef>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>
#include <unistd.h>

// Indicates the process wants to send its results and recv a new piece
#define REQUEST 1
#define DISMISS 2

#define SENDING_BLOCK 10
#define NO_MORE_BLOCKS 20

#define DATA_STATUS_NOT_USED 100
#define DATA_STATUS_USING 101
#define DATA_STATUS_SENDING 102
#define DATA_STATUS_SENT 103

#define RESULT_STATUS_NOT_READY 200
#define RESULT_STATUS_READY 201

#define NEXT_DATA_NONE 0
#define NEXT_RESULT_NONE 0

MPI_Aint globalStorageDisp;


// La estructura de la memoria de todo esto debería ser -> 
// Is there mem (byte)
// Is mem used (byte)
// Edges len -> Edges
// Weights Len -> Weights
// Selected Led -> Selected
//
// This way it is possible to search around to find unused memory

typedef struct _HMStorage
{
  int totalLen; // If this size = 0 then last node (everything is empty)
  int nodeId;
  int entries;

  // An array of:
  
  // key of hm val
  //
  // On the dat
  // dat len (byte)
  // dat buffer
  // key len (byte)
  // key buffer
  // does next exist(?)???

} HMStorage;

typedef struct _HMProcStorage
{
  MPI_Aint disp; // Disp of next HM
  int totalLen;
  int nodeId;
  int entries;

  // Tha same as HMStorage
} HMProcStorage;

typedef struct _InitNode
{
  MPI_Aint status;
  MPI_Aint disp; // This is the address of the first HMStorage

} InitNode;

typedef struct _WinStorage
{
  MPI_Aint totalSize;
  MPI_Aint nextData;
  MPI_Aint dataStatus;

  MPI_Aint edgesLen;
  MPI_Aint weigthsLen;
  MPI_Aint selectedLen;
  
  // int* edges
  // float* weights
  // int* selected

} WinStorage;

void putSelectedResults(const PWNet& ProcNet, MPI_Win storageWindow, MPI_Win resultWindow, MPI_Aint disp, MPI_Aint dispResult, int selectedLen, int* selectedBuff);
void putSelectedResultsList(const PWNet& ProcNet, InitNode& initNode, MPI_Win storageWindow, MPI_Win procResultWindow, int selectedLen, int* selectedBuff);
void appendSelectedResults(const PWNet& ProcNet, MPI_Win procResultWindow, MPI_Aint* toModifyDisp, int selectedLen, int* selectedBuff);
HMProcStorage* allocHMProcStorage(const TIntIntVFltVPrH& hash, const int& nodeId);
void PreprocessNodeParallel(PWNet &InNet, const double &ParamP, const double &ParamQ, TWNet::TNodeI NI);

bool isDataConsumed(WinStorage** winStorages, int blocks){
  return winStorages[blocks-1]->dataStatus == DATA_STATUS_SENT;
}

/*
TSize initResultStorage(const PWNet& InNet, HMStorage** results){
  TSize totalMem = sizeof(MPI_Aint); // Size req for the shared displs
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    TIntIntVFltVPrH hash = InNet->GetNDat(NI.GetId());
    for (THashKeyDatI<TInt, TIntVFltVPr> i = hash.BegI(); !i.IsEnd(); i++) {
      // Every entry has size =>
      // key (TINT)
      // dat (size (TInt) + size*(TInt) + size(TInt) + size*(TFlt))
       
      // This is the key
      
      // Base size with total-len and nodeId
      totalMem += sizeof(HMStorage);
      

      TIntVFltVPr dat = i.GetDat();
      TIntV intV = dat.GetVal1();
      TFltV intF = dat.GetVal2();

      // Size of the IntV and the IntV values
      totalMem += sizeof(TInt) + intV.Len()*sizeof(TInt);
      
      // Size of the FltV and the TFlt values
      totalMem += sizeof(TInt) + intF.Len()*sizeof(TFlt);
      
    }
  }

  *results = (HMStorage*)malloc(totalMem);
  return totalMem;
}
*/

TSize initResultStorage(const PWNet& InNet, HMStorage** results){
  // Start with 8 bytes for the global offset (MPI_Aint)
  TSize totalMem = sizeof(MPI_Aint); 
for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    totalMem += 3 * sizeof(TInt); // Node Header
    
    // For each neighbor (which will become a key in the hash)
    // This is artificially incrementing the size
    for (int e = 0; e < NI.GetDeg(); e++) {
        totalMem += sizeof(TInt); // Key
        // IntV: Len + (Degree * sizeof(TInt))
        totalMem += sizeof(TInt) + (NI.GetOutDeg() * sizeof(TInt));
        // FltV: Len + (Degree * sizeof(TFlt))
        totalMem += sizeof(TInt) + (NI.GetOutDeg() * sizeof(TFlt));
    }
}

  *results = (HMStorage*)malloc(totalMem);
  printf("Results:\naddress: %p\nsize: %ld\n\n", *results, totalMem);
  return totalMem;
}

/*
TSize initResultStorage(const PWNet& InNet, int selectedLen, int* selectedBuff, HMStorage* results){
  TSize totalMem = 0;
  for (int i = 0; i < selectedLen; i++) {
    
    TIntIntVFltVPrH hash = InNet->GetNDat(selectedBuff[i]);
    totalMem += sizeof(HMStorage);
    for (THashKeyDatI<TInt, TIntVFltVPr> i = hash.BegI(); !i.IsEnd(); i++) {
      // Every entry has size =>
      // key (TINT)
      // dat (size (TInt) + size*(TInt) + size(TInt) + size*(TFlt))
       
      
      // Base size with total-len and nodeId
      
      // This is the key
      totalMem += sizeof(TInt);

      TIntVFltVPr dat = i.GetDat();
      TIntV intV = dat.GetVal1();
      TFltV intF = dat.GetVal2();

      // Size of the IntV and the IntV values
      totalMem += sizeof(TInt) + intV.Len()*sizeof(TInt);
      
      // Size of the FltV and the TFlt values
      totalMem += sizeof(TInt) + intF.Len()*sizeof(TFlt);
      
    }
  }

  results = (HMStorage*)malloc(totalMem);
  return totalMem;
}
*/

WinStorage* initWinStorage(int edgesLen, int weightsLen, int selectedLen){
  
  size_t totalSize =
    sizeof(WinStorage)
    + edgesLen * sizeof(int)
    + weightsLen * sizeof(float)
    + selectedLen * sizeof(int);

  // Add this to storeWinStorage to ensure 8-byte alignment
  size_t paddedSize = (totalSize + 7) & ~7; 
  WinStorage* storage = (WinStorage*) malloc(paddedSize);
  storage->totalSize = paddedSize;
  storage->nextData = NEXT_DATA_NONE;
  storage->dataStatus = DATA_STATUS_NOT_USED;
  storage->edgesLen = edgesLen;
  storage->weigthsLen = weightsLen;
  storage->selectedLen = selectedLen;

  /* Propper way of getting those buffers
  int* edges = (int*)(storage+1);
  float* weights = (float*)(edges + edgesLen);
  int* selected = (int*)(weights + weightsLen);
  */

  return storage;
}

WinStorage* storeWinStorage(const THash<TInt, TBool>& Selected, const THash<TPair<TInt, TInt>, TFlt>& Edges){
  int SelectedLen = Selected.Len();
  int EdgesLen = Edges.Len();
  
  WinStorage* storage = initWinStorage(EdgesLen*2, EdgesLen, SelectedLen);
  int* edges = (int*)(storage+1);
  float* weights = (float*)(edges+storage->edgesLen);
  int* selected = (int*)(weights+storage->weigthsLen);

  int j = 0;
  for (THash<TInt, TBool>::TIter i = Selected.BegI(); !i.IsEnd(); i.Next()) {
    selected[j] = i.GetKey();
    j++;
  }

  j = 0;
  for (THash<TPair<TInt, TInt>, TFlt>::TIter i = Edges.BegI(); !i.IsEnd(); i.Next()) {
    TPair<TInt, TInt> Edge = i.GetKey();
    int k = j * 2;
    edges[k] = Edge.GetVal1();
    edges[k + 1] = Edge.GetVal2();
    weights[j] = i.GetDat();
    j++;
  }
  return storage;
}

int getFirstAvailableRank0(PWNet& InNet, MPI_Win storageWindow, MPI_Win resultWindow, MPI_Win procResultWindow, MPI_Aint disp, MPI_Aint dispResult, InitNode& initNode, const double& ParamP, const double& ParamQ, int blocks){
MPI_Aint vals[3]; // [0] total size, [1] next data, [2] status
int currentWindow = 0;
bool moreWindows = true;

WinStorage* storage = NULL;

MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, storageWindow);
MPI_Win_flush(0, storageWindow);

  while(moreWindows){
    if(disp == NEXT_DATA_NONE){
      if(storage != NULL) free(storage);
      break;
    }
    MPI_Get(vals,
            3,
            MPI_AINT,
            0,
            disp,
            3,
            MPI_AINT,
            storageWindow
            );
    MPI_Win_flush(0, storageWindow);
    //printf("\nBlock %d, From %p Gotten:\nvals[0]size: %ld\nvals[1]nextData: %p\nvals[2]status %ld\n\n", currentWindow, disp, vals[0], vals[1], vals[2]);
    fflush(stdout);
  
    if(vals[2] == DATA_STATUS_NOT_USED){
      storage = (WinStorage*) malloc(vals[0]);
    //printf("\ngetFirst 2 Get where storage: %p\nvals0: %ld\ndisp: %p\n\n", storage, vals[0], disp);
    fflush(stdout);
      // TODO: Fix this Get
      MPI_Get(storage,
              vals[0],
              MPI_BYTE,
              0,
              disp,
              vals[0],
              MPI_BYTE,
              storageWindow);
    MPI_Win_flush(0, storageWindow);
    //printf("getFirst 3 Put\n");
    fflush(stdout);
      MPI_Aint status = DATA_STATUS_USING;
      MPI_Aint statusByteOffset;
      statusByteOffset = offsetof(WinStorage, dataStatus);
      MPI_Put(
        &status,
        1,
        MPI_AINT,
        0,
        disp+statusByteOffset, // This should edit the status only
        1,
        MPI_AINT,
        storageWindow
      );
      MPI_Win_flush(0, storageWindow);
      break;
    } else if(vals[1] == NEXT_DATA_NONE || vals[1] == NULL){
      printf("No data next, curr: %d, blocks-1: %d\n", currentWindow, blocks-1);
      fflush(stdout);
      if(currentWindow == blocks-1){
        if(storage != NULL){
          free(storage);
        }
        // Unlock before returning
        MPI_Win_unlock(0, storageWindow);
        return -1; // Processing should end now
        }
        break;
      } else {
        //printf("Reassing disp to: %p\n", vals[1]);
        disp = vals[1];
        currentWindow++;
      }
    }

  MPI_Win_flush(0, storageWindow);
  MPI_Win_unlock(0, storageWindow);
  MPI_Win_sync(storageWindow);
  
  if(storage == NULL) return 1; // This means we should try again No need to re-create the procNet
  
  int edgesLen = storage->edgesLen;
  int weightsLen = storage->weigthsLen;
  int selectedLen = storage->selectedLen;

  int* edges = (int*)(storage+1);
  float* weights = (float*)(edges + edgesLen);
  int* selected = (int*)(weights + weightsLen);
  
  
  clock_t begin = clock();
    double begin_nat = omp_get_wtime();

  for (int i = 0; i < selectedLen; i++) {
    PreprocessNodeParallel(InNet, ParamP, ParamQ,
                           InNet->GetNI((selected)[i]));
  }

    clock_t end = clock();
    double end_nat = omp_get_wtime();

    double _time = double(end - begin) / CLOCKS_PER_SEC;
    double _time_nat = end_nat - begin_nat;

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  printf("<proc rank=\"%d\" process=\"%f\" natural=\"%f\" />\n", rank, _time, _time_nat);

  if(storage != NULL){
    free(storage);
  }
  return 0;
}

// Some way of returning the actual procNet here please.
int getFirstAvailable(MPI_Win storageWindow, MPI_Win resultWindow, MPI_Win procResultWindow, MPI_Aint disp, MPI_Aint dispResult, InitNode& initNode, const double& ParamP, const double& ParamQ, int blocks){
MPI_Aint vals[3]; // [0] total size, [1] next data, [2] status
int currentWindow = 0;
bool moreWindows = true;

WinStorage* storage = NULL;

MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, storageWindow);
MPI_Win_flush(0, storageWindow);

  while(moreWindows){
    if(disp == NEXT_DATA_NONE){
      if(storage != NULL) free(storage);
      break;
    }
    MPI_Get(vals,
            3,
            MPI_AINT,
            0,
            disp,
            3,
            MPI_AINT,
            storageWindow
            );
    MPI_Win_flush(0, storageWindow);
    //printf("\nBlock %d, From %p Gotten:\nvals[0]size: %ld\nvals[1]nextData: %p\nvals[2]status %ld\n\n", currentWindow, disp, vals[0], vals[1], vals[2]);
    fflush(stdout);
  
    if(vals[2] == DATA_STATUS_NOT_USED){
      storage = (WinStorage*) malloc(vals[0]);
    //printf("\ngetFirst 2 Get where storage: %p\nvals0: %ld\ndisp: %p\n\n", storage, vals[0], disp);
    fflush(stdout);
      // TODO: Fix this Get
      MPI_Get(storage,
              vals[0],
              MPI_BYTE,
              0,
              disp,
              vals[0],
              MPI_BYTE,
              storageWindow);
    MPI_Win_flush(0, storageWindow);
    //printf("getFirst 3 Put\n");
    fflush(stdout);
      MPI_Aint status = DATA_STATUS_USING;
      MPI_Aint statusByteOffset;
      statusByteOffset = offsetof(WinStorage, dataStatus);
      MPI_Put(
        &status,
        1,
        MPI_AINT,
        0,
        disp+statusByteOffset, // This should edit the status only
        1,
        MPI_AINT,
        storageWindow
      );
      MPI_Win_flush(0, storageWindow);
      break;
    } else if(vals[1] == NEXT_DATA_NONE || vals[1] == NULL){
      int rank;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      printf("Rank %d. No data next, curr: %d, blocks-1: %d\n", rank, currentWindow, blocks-1);
      fflush(stdout);
      if(currentWindow == blocks-1){
        if(storage != NULL){
          free(storage);
        }
        // Unlock before returning
        MPI_Win_unlock(0, storageWindow);
        return -1; // Processing should end now
        }
        break;
      } else {
        //printf("Reassing disp to: %p\n", vals[1]);
        disp = vals[1];
        currentWindow++;
      }
    }

  MPI_Win_flush(0, storageWindow);
  MPI_Win_unlock(0, storageWindow);
  
  if(storage == NULL) return 1; // This means we should try again No need to re-create the procNet
  
  int edgesLen = storage->edgesLen;
  int weightsLen = storage->weigthsLen;
  int selectedLen = storage->selectedLen;

  int* edges = (int*)(storage+1);
  float* weights = (float*)(edges + edgesLen);
  int* selected = (int*)(weights + weightsLen);
  
  PWNet ProcNet = PWNet::New();

  for (int i = 0; i <  weightsLen; i++) {
    int node1, node2;
    node1 = edges[i * 2];
    node2 = edges[i * 2 + 1];

    if (!ProcNet->IsNode(node1)) {
      ProcNet->AddNode(node1);
    }
    if (!ProcNet->IsNode(node2)) {
      ProcNet->AddNode(node2);
    }

    ProcNet->AddEdge(node1, node2, weights[i]);
  }
  // Return Selected in some incredible way
  
  clock_t begin = clock();
    double begin_nat = omp_get_wtime();



  for (int i = 0; i < selectedLen; i++) {
    PreprocessNodeParallel(ProcNet, ParamP, ParamQ,
                           ProcNet->GetNI((selected)[i]));
  }

    clock_t end = clock();
    double end_nat = omp_get_wtime();

    double _time = double(end - begin) / CLOCKS_PER_SEC;
    double _time_nat = end_nat - begin_nat;

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  printf("<proc rank=\"%d\" process=\"%f\" natural=\"%f\" />\n", rank, _time, _time_nat);

  putSelectedResults(ProcNet, storageWindow, resultWindow, disp, dispResult, selectedLen, selected);

  printf("Put again\n");
  fflush(stdout);
  //putSelectedResultsList(ProcNet, initNode, storageWindow, procResultWindow, selectedLen, selected);

  if(storage != NULL){
    free(storage);
  }
  return 0;
}

void putSelectedResultsList(const PWNet& ProcNet, InitNode& initNode, MPI_Win storageWindow, MPI_Win procResultWindow, int selectedLen, int* selectedBuff){
  /*
    The list will be: InitNode

    The nodes will be of type HMProcStorage (it has now a disp on the first memory MPI_Aint)
  */
  printf("Agrs:\n");
  printf("initNode: %ld, %ld\n", initNode.disp, initNode.status);
  fflush(stdout);

  MPI_Aint* disp = &(initNode.disp);
  if(*disp == NEXT_RESULT_NONE || *disp == NULL){
    // Alloc and place disp
    printf("First append\n");
    fflush(stdout);
    appendSelectedResults(ProcNet, procResultWindow, disp, selectedLen, selectedBuff);
  } else{

    while(*disp != NEXT_RESULT_NONE && *disp != NULL){
      void* location = (void*)*disp;
      HMProcStorage* block = (HMProcStorage*)location;
      disp = &(block->disp);
    }
    // Gotten to last node. Append new node
    printf("Subsequent append\n");
    fflush(stdout);
    appendSelectedResults(ProcNet, procResultWindow, disp, selectedLen, selectedBuff);
  }

}

void appendSelectedResults(const PWNet& ProcNet, MPI_Win procResultWindow, MPI_Aint* toModifyDisp, int selectedLen, int* selectedBuff){
  MPI_Aint* previousPtr = toModifyDisp;
  for(int i = 0; i < selectedLen; i++){
    int nodeId = selectedBuff[i];
    TIntIntVFltVPrH hash = ProcNet->GetNDat(nodeId);

    // Alloc space
    printf("Alloc space\n");
    fflush(stdout);
    HMProcStorage* hmPointer = allocHMProcStorage(hash, nodeId);
    MPI_Win_attach(procResultWindow, hmPointer, hmPointer->totalLen);
    MPI_Aint disp;
    MPI_Get_address(hmPointer, &disp);
    *previousPtr = disp;
    
    hmPointer->disp = NEXT_RESULT_NONE;
    previousPtr = &(hmPointer->disp);
  }
  
}

HMProcStorage* allocHMProcStorage(const TIntIntVFltVPrH& hash,  const int& nodeId){
  size_t totalBufferSize = 0;
  
  // Add the disp size
  totalBufferSize += sizeof(MPI_Aint);

  // Header overhead per node (totalSize + nodeId + numEntries)
  totalBufferSize += sizeof(TInt) * 3; 

  for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {
    const TIntV& intVec = it.GetDat().Val1;
    const TFltV& fltVec = it.GetDat().Val2;
    totalBufferSize += sizeof(TInt);            // key
    totalBufferSize += sizeof(TInt);            // TIntV length
    totalBufferSize += sizeof(TInt) * intVec.Len();
    totalBufferSize += sizeof(TInt);            // TFltV length
    totalBufferSize += sizeof(TFlt) * fltVec.Len();
  }


  // --- PART 2: Serialize everything into one local buffer ---
  char* bigBuffer = (char*)malloc(totalBufferSize);
  char* ptr = bigBuffer;

  int numEntries = hash.Len();

  // Temporarily save position to write node-specific totalSize later
  MPI_Aint* nextDispPtr = (MPI_Aint*)ptr; ptr += sizeof(MPI_Aint);
  char* nodeSizePtr = ptr;
  ptr += sizeof(TInt); // Skip totalSize for now
  *((TInt*)ptr) = nodeId;     ptr += sizeof(TInt);
  *((TInt*)ptr) = numEntries; ptr += sizeof(TInt);

  char* entriesStart = ptr;
  for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {
    int key = it.GetKey();
    const TIntV& intVec = it.GetDat().Val1;
    const TFltV& fltVec = it.GetDat().Val2;

    *((TInt*)ptr) = key; ptr += sizeof(TInt);
    
    *((TInt*)ptr) = intVec.Len(); ptr += sizeof(TInt);
    memcpy(ptr, intVec.BegI(), sizeof(TInt) * intVec.Len());
    ptr += sizeof(TInt) * intVec.Len();

    *((TInt*)ptr) = fltVec.Len(); ptr += sizeof(TInt);
    memcpy(ptr, fltVec.BegI(), sizeof(TFlt) * fltVec.Len());
    ptr += sizeof(TFlt) * fltVec.Len();
  }

  // After writing all entries for one node
  // Write the actual size for this specific node block
  *((TInt*)nodeSizePtr) = (TInt)(ptr - nodeSizePtr);
  return (HMProcStorage* )bigBuffer;
}


/*
void putSelectedResults(const PWNet& ProcNet, MPI_Win storageWindow, MPI_Win resultWindow, MPI_Aint disp, MPI_Aint dispResult, int selectedLen, int* selectedBuff) {

  for (int i = 0; i < selectedLen; ++i) {

    int nodeId = selectedBuff[i];
    TIntIntVFltVPrH hash = ProcNet->GetNDat(nodeId);

    // First compute required size
    int totalSize = sizeof(TInt) * 3; 
    // totalLen + nodeId + numEntries

    int numEntries = hash.Len();

    for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {
      const TIntV& intVec = it.GetDat().Val1;
      const TFltV& fltVec = it.GetDat().Val2;

      totalSize += sizeof(TInt); // key
      totalSize += sizeof(TInt); // TIntV length
      totalSize += sizeof(TInt) * intVec.Len();
      totalSize += sizeof(TInt); // TFltV length
      totalSize += sizeof(TFlt) * fltVec.Len();
    }

    // Allocate contiguous storage
    char* buffer = (char*) malloc(totalSize);
    char* ptr = buffer;

    // Write header
    *((TInt*)ptr) = totalSize; ptr += sizeof(TInt);
    *((TInt*)ptr) = nodeId;    ptr += sizeof(TInt);
    *((TInt*)ptr) = numEntries; ptr += sizeof(TInt);

    // Write entries
    for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {

      int key = it.GetKey();
      const TIntV& intVec = it.GetDat().Val1;
      const TFltV& fltVec = it.GetDat().Val2;

      // key
      *((TInt*)ptr) = key;
      ptr += sizeof(TInt);

      // TIntV
      *((TInt*)ptr) = intVec.Len();
      ptr += sizeof(TInt);
      memcpy(ptr, intVec.BegI(), sizeof(TInt) * intVec.Len());
      ptr += sizeof(TInt) * intVec.Len();

      // TFltV
      *((TInt*)ptr) = fltVec.Len();
      ptr += sizeof(TInt);
      memcpy(ptr, fltVec.BegI(), sizeof(TFlt) * fltVec.Len());
      ptr += sizeof(TFlt) * fltVec.Len();
    }

         
    printf("Hey\n");
    fflush(stdout);
    // TODO: Fix a crash here upon second put
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, resultWindow);

    MPI_Aint currOffset;
    // 1. Get the current RELATIVE offset from the start of the buffer
    MPI_Get(&currOffset, 1, MPI_AINT, 0, dispResult, 1, MPI_AINT, resultWindow);
    MPI_Win_flush(0, resultWindow);
    
    // 2. Calculate the next offset for the next person
    MPI_Aint nextOffset = currOffset + totalSize;
    MPI_Put(&nextOffset, 1, MPI_AINT, 0, dispResult, 1, MPI_AINT, resultWindow);
    
    // 3. Put the data at: Base Address + Relative Offset
    // This is now: 0x7000 + 8 (Correct!) instead of 0x7000 + 0x7000 (Crash!)
    MPI_Put(buffer, totalSize, MPI_BYTE, 0, dispResult + currOffset, totalSize, MPI_BYTE, resultWindow);
    
    MPI_Win_unlock(0, resultWindow);

    MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, storageWindow);

    MPI_Aint status = DATA_STATUS_SENT;
    MPI_Put(
      &status,
      1,
      MPI_AINT,
      0,
      disp+2,
      1,
      MPI_AINT,
      storageWindow
    );


    MPI_Win_unlock(0, storageWindow);

    printf("Hey\n");
    fflush(stdout);

    free(buffer);
  }
}
*/

void putSelectedResults(const PWNet& ProcNet, MPI_Win storageWindow, MPI_Win resultWindow, MPI_Aint disp, MPI_Aint dispResult, int selectedLen, int* selectedBuff) {
  // --- PART 1: Calculate Total Required Size for ALL nodes ---
  size_t totalBufferSize = 0;
  for (int i = 0; i < selectedLen; ++i) {
    int nodeId = selectedBuff[i];
    TIntIntVFltVPrH hash = ProcNet->GetNDat(nodeId);
    
    // Header overhead per node (totalSize + nodeId + numEntries)
    totalBufferSize += sizeof(TInt) * 3; 

    for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {
      const TIntV& intVec = it.GetDat().Val1;
      const TFltV& fltVec = it.GetDat().Val2;
      totalBufferSize += sizeof(TInt);            // key
      totalBufferSize += sizeof(TInt);            // TIntV length
      totalBufferSize += sizeof(TInt) * intVec.Len();
      totalBufferSize += sizeof(TInt);            // TFltV length
      totalBufferSize += sizeof(TFlt) * fltVec.Len();
    }
  }

  totalBufferSize = (totalBufferSize + 7) & ~7;

  // --- PART 2: Serialize everything into one local buffer ---
  char* bigBuffer = (char*)malloc(totalBufferSize);
  char* ptr = bigBuffer;

  for (int i = 0; i < selectedLen; ++i) {
    int nodeId = selectedBuff[i];
    TIntIntVFltVPrH hash = ProcNet->GetNDat(nodeId);
    int numEntries = hash.Len();

    // Temporarily save position to write node-specific totalSize later
    char* nodeSizePtr = ptr; 
    ptr += sizeof(TInt); // Skip totalSize for now
    *((TInt*)ptr) = nodeId;     ptr += sizeof(TInt);
    *((TInt*)ptr) = numEntries; ptr += sizeof(TInt);

    char* entriesStart = ptr;
    for (TIntIntVFltVPrH::TIter it = hash.BegI(); it < hash.EndI(); it++) {
      int key = it.GetKey();
      const TIntV& intVec = it.GetDat().Val1;
      const TFltV& fltVec = it.GetDat().Val2;

      *((TInt*)ptr) = key; ptr += sizeof(TInt);
      
      *((TInt*)ptr) = intVec.Len(); ptr += sizeof(TInt);
      memcpy(ptr, intVec.BegI(), sizeof(TInt) * intVec.Len());
      ptr += sizeof(TInt) * intVec.Len();

      *((TInt*)ptr) = fltVec.Len(); ptr += sizeof(TInt);
      memcpy(ptr, fltVec.BegI(), sizeof(TFlt) * fltVec.Len());
      ptr += sizeof(TFlt) * fltVec.Len();
    }

  // After writing all entries for one node
    // Write the actual size for this specific node block
    *((TInt*)nodeSizePtr) = (TInt)(ptr - nodeSizePtr);
  }

  // --- PART 3: Single Remote Memory Access ---
  MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, resultWindow);

  MPI_Aint currOffset;
  // Get current global displacement (stored at Rank 0 base address)
  //printf("First Get\n");
  fflush(stdout);
  MPI_Get(&currOffset, 1, MPI_AINT, 0, dispResult, 1, MPI_AINT, resultWindow);
  MPI_Win_flush(0, resultWindow);
  
  // Calculate and update the next available offset
  //printf("First Put\n");
  fflush(stdout);
  MPI_Aint nextOffset = currOffset + totalBufferSize;
  MPI_Put(&nextOffset, 1, MPI_AINT, 0, dispResult, 1, MPI_AINT, resultWindow);
  
  // Single write of all data to: Base Address + Reserved Offset
  //printf("Secont Put in:\naddress: %p\nsize: %p\n", dispResult + currOffset, totalBufferSize);
  fflush(stdout);
  MPI_Put(bigBuffer, totalBufferSize, MPI_BYTE, 0, dispResult + currOffset, totalBufferSize, MPI_BYTE, resultWindow);
  
  MPI_Win_unlock(0, resultWindow);

  // --- PART 4: Update Block Status ---
  MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, storageWindow);
  MPI_Aint status = DATA_STATUS_SENT;
  //printf("Third Put\n");
  fflush(stdout);
  MPI_Aint statusByteOffset = offsetof(WinStorage, dataStatus);
  MPI_Put(&status, 1, MPI_AINT, 0, disp + statusByteOffset, 1, MPI_AINT, storageWindow);
  MPI_Win_unlock(0, storageWindow);

  free(bigBuffer);
}

void gatherResults(PWNet& ProcNet, MPI_Win resultWindow){
  // Do something here
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



// Distribution of graph between procs
// GIVES ERRORS WITH 1 RANK ONLY
void DistributeGraph(PWNet &InNet, int NumProcs, int Blocks,
                     THash<TInt, TBool> &SelectedRank0, PWNet &ProcNet0, MPI_Win storageWindow, WinStorage** winStorages) {

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

    
    // Store result into window
    WinStorage* storage = storeWinStorage(Selected, Edges);
    storage->nextData = NEXT_DATA_NONE;
    winStorages[i] = storage;
    MPI_Win_attach(storageWindow, storage, storage->totalSize);
    MPI_Aint disp;
    MPI_Get_address(storage, &disp);

    printf("\nLocation: %p\nSize: %ld\n\n", storage, storage->totalSize);
    fflush(stdout);
    if(i == 0){ // On first one send the displs, later add it to previous block
      MPI_Bcast(&disp, 1, MPI_AINT, 0, MPI_COMM_WORLD);
      globalStorageDisp = disp;
    } else {
      winStorages[i-1]->nextData = disp;
      MPI_Win_sync(storageWindow);
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
                               const double &ParamQ, const int& Blocks_original, const bool &verbose) {

  int rank, numprocs;
  int Blocks = Blocks_original;

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
  
  // Create RMA window
  MPI_Win storageWindow;
  MPI_Win_create_dynamic(MPI_INFO_NULL, MPI_COMM_WORLD, &storageWindow);
  
  // Create window for receiving data
  // This will probably have to be divided so that all my storage does not explode
  
  // The very first value of this window should be the actual displ to be used
  MPI_Win resultWindow;
  MPI_Win_create_dynamic(MPI_INFO_NULL, MPI_COMM_WORLD, &resultWindow);
  
  // TODO: maybe there is a need for one window for process
  MPI_Win procResultWindow;
  MPI_Win_create_dynamic(MPI_INFO_NULL, MPI_COMM_WORLD, &procResultWindow);
  
  // Create the init nodes
  MPI_Aint procResultDisp[numprocs];
  InitNode initNode;
  for(int i = 1; i < numprocs; i++){
    if(rank == i){
      initNode.disp = NEXT_RESULT_NONE;
      initNode.status = RESULT_STATUS_NOT_READY;
      MPI_Win_attach(procResultWindow, &initNode, sizeof(initNode));

      MPI_Get_address(&initNode, &(procResultDisp[i]));
      MPI_Win_sync(procResultWindow);
    }
    MPI_Bcast(&(procResultDisp[i]), 1, MPI_AINT, 0, MPI_COMM_WORLD);
  }
  
  // Share Blocks
  MPI_Bcast(&Blocks, 1, MPI_INT, 0, MPI_COMM_WORLD);
  

  int SelectedLen;
  int *SelectedBuff;

  // Available during all execution
  HMStorage* results;
  TSize totalSize = 0;

  PWNet ProcNet;
  THash<TInt, TBool> SelectedRank0;
  if (rank == 0) {

    totalSize = initResultStorage(InNet, &results);

    // Alloc everything to be received
    MPI_Win_attach(resultWindow, results, totalSize);

    MPI_Aint dispResult;
    MPI_Get_address(results, &dispResult);

    *((MPI_Aint*)results) = (MPI_Aint)sizeof(MPI_Aint);

    MPI_Win_sync(resultWindow);
    MPI_Bcast(&dispResult, 1, MPI_AINT, 0, MPI_COMM_WORLD);

    int BlocksSent = 0;
    // Blocks of storage
    // Each will be individually alloc'd if needed
    
    WinStorage** winStorages = (WinStorage**)malloc(sizeof(WinStorage*) * Blocks);

    clock_t begin = clock();
    double begin_nat = omp_get_wtime();

    DistributeGraph(InNet, numprocs, Blocks, SelectedRank0, ProcNet, storageWindow, winStorages);

    clock_t end = clock();
    double end_nat = omp_get_wtime();

    double _time = double(end - begin) / CLOCKS_PER_SEC;
    double _time_nat = end_nat - begin_nat;

    printf("<distribution process=\"%f\" natural=\"%f\" />", _time, _time_nat);
    
    bool moreBlocks = true;
              
    while(moreBlocks){

      printf("Before get first-> disp: %p, blocks: %d\n\n", globalStorageDisp, Blocks);
      int result = getFirstAvailableRank0(InNet, storageWindow, resultWindow, procResultWindow, globalStorageDisp, dispResult, initNode, ParamP, ParamQ, Blocks);
      
      printf("Result: %d\n", result);
      if(result == -1){
        moreBlocks = false;
        printf("GOT OUT\n");
      } else if (result == 1){
        usleep(5000);
        // do nothing, should retry the operation please, but wait a moment;
        // TODO: Please do something to wait here
      } else {
        // Everything went right so nothing to do
        // Already put everything
      }

      

    }
    printf("GOT OUT\n");
    fflush(stdout);

    // Start Recv for all remaining Procs
    /*
    while(!isDataConsumed(winStorages, Blocks)){
      printf("Stuck\n");
      fflush(stdout);

      
      MPI_Win_sync(resultWindow);
      MPI_Win_sync(storageWindow);
      
      usleep(500000);

    }
    */
    printf("GOT HERE TOO\n");
    fflush(stdout);
    // Dismiss processes
      // TODO : Do something with the results vector
      
    // Base address
    char* base = (char*)results;
    char* ptr = base;
    
    // Before your while loop, read the actual amount of data written:
    MPI_Aint finalOffset;
    MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, resultWindow);
    MPI_Get(&finalOffset, 1, MPI_AINT, 0, dispResult, 1, MPI_AINT, resultWindow);
    MPI_Win_flush(0, resultWindow);
    MPI_Win_unlock(0, resultWindow);


    // Skip the first 8 bytes (which hold the finalOffset itself)
    ptr = base + sizeof(MPI_Aint);

    // Loop only up to the data that was actually written
    
    // Loop up to the data that was actually written
while(ptr - base < finalOffset) {

    // 1. PADDING GUARD: 
    // If the remaining space is smaller than our basic header (3 * TInt), 
    // we have hit the 8-byte padding at the end of the buffer. Stop reading.
    if (finalOffset - (ptr - base) < (MPI_Aint)(3 * sizeof(TInt))) {
        break;
    }

    // Save the exact start position of this node block
    char* nodeStart = ptr;
    
    // Read Headers
    int totalSize = *((TInt*)ptr); ptr += sizeof(TInt);
    
    // Safety check: if totalSize is corrupted or 0 (padding), break out
    if (totalSize < 3 * sizeof(TInt)) {
        break;
    }

    int nodeId = *((TInt*)ptr); ptr += sizeof(TInt);
    int numEntries = *((TInt*)ptr); ptr += sizeof(TInt);

    printf("read -> %p | Node: %d | Entries: %d | Expected Size: %d\n", nodeStart - base, nodeId, numEntries, totalSize);
    fflush(stdout);
      
    TIntIntVFltVPrH hash;

    // Read entries
    for(int i = 0; i < numEntries; i++){
        int key = *((TInt*)ptr); ptr += sizeof(TInt);
      
        int intVLen = *((TInt*)ptr); ptr += sizeof(TInt);
        TIntV intV(intVLen);
        for(int j = 0; j < intVLen; j++){
            intV[j] = (*(TInt*)ptr);
            ptr += sizeof(TInt);
        }
          
        int fltVLen = *((TInt*)ptr); ptr += sizeof(TInt);
        TFltV fltV(fltVLen);
        for(int j = 0; j < fltVLen; j++){
            fltV[j] = (*(TFlt*)ptr);
            ptr += sizeof(TFlt);
        }
        hash.AddDat(key, TPair<TIntV, TFltV>(intV, fltV));
    }
    
    // TODO: Do whatever you need with 'hash' here before moving to the next node
    
    // 2. BLOCK GUARD: 
    // Force the pointer to jump exactly to where the NEXT node starts.
    // This ignores any inner-loop drift and relies on the accurate totalSize header.
    ptr = nodeStart + totalSize;    
}

  } else {
    
    MPI_Aint dispResult;
    MPI_Bcast(&dispResult, 1, MPI_AINT, 0, MPI_COMM_WORLD);
    // Start by getting the bcast that will also signal that the rma is active
    MPI_Aint disp;
    MPI_Bcast(&disp, 1, MPI_AINT, 0, MPI_COMM_WORLD);
    
    bool moreBlocks = true;
              
    while(moreBlocks){

      printf("Before get first-> disp: %p, blocks: %d\n\n", disp, Blocks);
      int result = getFirstAvailable(storageWindow, resultWindow, procResultWindow, disp, dispResult, initNode, ParamP, ParamQ, Blocks);
      
      if(result == -1){
        moreBlocks = false;
        printf("GOT OUT\n");
      } else if (result == 1){
        usleep(5000);
        // do nothing, should retry the operation please, but wait a moment;
        // TODO: Please do something to wait here
      } else {
        // Everything went right so nothing to do
        // Already put everything
      }

      

    }
    printf("GOT OUT\n");

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