#include "stdafx.h"
#include "Snap.h"
#include <ctime>
#include "word2vec.h"

//Code from https://github.com/nicholas-leonard/word2vec/blob/master/word2vec.c
//Customized for SNAP and node2vec

void LearnVocab(TVVec<TInt, int64>& WalksVV, TIntV& Vocab) {
  // Init vocab to 0
  for( int64 i = 0; i < Vocab.Len(); i++) { Vocab[i] = 0; }

  // Count the times we stepped on each node
  for( int64 i = 0; i < WalksVV.GetXDim(); i++) {
    for( int j = 0; j < WalksVV.GetYDim(); j++) {
      Vocab[WalksVV(i,j)]++;
    }
  }
}

//Precompute unigram table using alias sampling method
void InitUnigramTable(TIntV& Vocab, TIntV& KTable, TFltV& UTable) {
  double TrainWordsPow = 0;
  double Pwr = 0.75;
  TFltV ProbV(Vocab.Len());
  for (int64 i = 0; i < Vocab.Len(); i++) {
    ProbV[i]=TMath::Power(Vocab[i],Pwr);
    TrainWordsPow += ProbV[i];
    KTable[i]=0;
    UTable[i]=0;
  }
  for (int64 i = 0; i < ProbV.Len(); i++) {
    ProbV[i] /= TrainWordsPow;
  }
  TIntV UnderV;
  TIntV OverV;
  for (int64 i = 0; i < ProbV.Len(); i++) {
    UTable[i] = ProbV[i] * ProbV.Len();
    if ( UTable[i] < 1 ) {
      UnderV.Add(i);
    } else {
      OverV.Add(i);
    }
  }
  while(UnderV.Len() > 0 && OverV.Len() > 0) {
    int64 Small = UnderV.Last();
    int64 Large = OverV.Last();
    UnderV.DelLast();
    OverV.DelLast();
    KTable[Small] = Large;
    UTable[Large] = (UTable[Large] + UTable[Small]) - 1;
    if (UTable[Large] < 1) {
      UnderV.Add(Large);
    } else {
      OverV.Add(Large);
    }
  }
  while(UnderV.Len() > 0){
    int64 curr = UnderV.Last();
    UnderV.DelLast();
    UTable[curr]=1;
  }
  while(OverV.Len() > 0){
    int64 curr = OverV.Last();
    OverV.DelLast();
    UTable[curr]=1;
  }
}

int64 RndUnigramInt(TIntV& KTable, TFltV& UTable, TRnd& Rnd) {
  TInt X = KTable[static_cast<int64>(Rnd.GetUniDev()*KTable.Len())];
  double Y = Rnd.GetUniDev();
  return Y < UTable[X] ? X : KTable[X];
}

//Initialize negative embeddings
void InitNegEmb(TIntV& Vocab, const int& Dimensions, TVVec<TFlt, int64>& SynNeg) {
  SynNeg = TVVec<TFlt, int64>(Vocab.Len(),Dimensions);
  for (int64 i = 0; i < SynNeg.GetXDim(); i++) {
    for (int j = 0; j < SynNeg.GetYDim(); j++) {
      SynNeg(i,j) = 0;
    }
  }
}

//Initialize positive embeddings
void InitPosEmb(TIntV& Vocab, const int& Dimensions, TRnd& Rnd, TVVec<TFlt, int64>& SynPos) {
  SynPos = TVVec<TFlt, int64>(Vocab.Len(),Dimensions);
  for (int64 i = 0; i < SynPos.GetXDim(); i++) {
    for (int j = 0; j < SynPos.GetYDim(); j++) {
      SynPos(i,j) =(Rnd.GetUniDev()-0.5)/Dimensions;
    }
  }
}

void TrainModel(TVVec<TInt, int64>& WalksVV, const int& Dimensions,
    const int& WinSize, const int& Iter, const bool& Verbose,
    TIntV& KTable, TFltV& UTable, int64& WordCntAll, TFltV& ExpTable,
    double& Alpha, int64 CurrWalk, TRnd& Rnd,
    TVVec<TFlt, int64>& SynNeg, TVVec<TFlt, int64>& SynPos)  {
  TFltV Neu1V(Dimensions);
  TFltV Neu1eV(Dimensions);
  int64 AllWords = WalksVV.GetXDim()*WalksVV.GetYDim();
  TIntV WalkV(WalksVV.GetYDim());

  // Divide into separate walks
  for (int j = 0; j < WalksVV.GetYDim(); j++) { WalkV[j] = WalksVV(CurrWalk,j); }

  // For each node do everything
  #pragma omp ordered
  {
  for (int64 WordI=0; WordI<WalkV.Len(); WordI++) {

    if ( WordCntAll%10000 == 0 ) {
      if ( Verbose ) {
        printf("\rLearning Progress: %.2lf%% ",(double)WordCntAll*100/(double)(Iter*AllWords));
        fflush(stdout);
      }
      Alpha = StartAlpha * (1 - WordCntAll / static_cast<double>(Iter * AllWords + 1));
      if ( Alpha < StartAlpha * 0.0001 ) { Alpha = StartAlpha * 0.0001; }
    }

    int64 Word = WalkV[WordI];
    // Init Neu1V and Neu1eV
    for (int i = 0; i < Dimensions; i++) {
      Neu1V[i] = 0;
      Neu1eV[i] = 0;
    }

    // WordI is the current node of the walk
    //int Offset = Rnd.GetUniDevInt() % WinSize;
    int Offset = WinSize/2;
    for (int a = Offset; a < WinSize * 2 + 1 - Offset; a++) {
      if (a == WinSize) { continue; }
      int64 CurrWordI = WordI - WinSize + a;
      if (CurrWordI < 0){ continue; }
      if (CurrWordI >= WalkV.Len()){ continue; }
      int64 CurrWord = WalkV[CurrWordI];
      
      // Init this vector to 0
      for (int i = 0; i < Dimensions; i++) { Neu1eV[i] = 0; }

      //negative sampling
      for (int j = 0; j < NegSamN+1; j++) {
        int64 Target, Label;
        if (j == 0) {
          Target = Word;
          Label = 1;
        } else {
          TRnd Rnd2(1);
          Target = RndUnigramInt(KTable, UTable, Rnd2);
          if (Target == Word) { continue; }
          Label = 0;
        }
        double Product = 0;
        for (int i = 0; i < Dimensions; i++) {
          Product += SynPos(CurrWord,i) * SynNeg(Target,i);
        }
        double Grad;                     //Gradient multiplied by learning rate
        if (Product > MaxExp) { Grad = (Label - 1) * Alpha; }
        else if (Product < -MaxExp) { Grad = Label * Alpha; }
        else { 
          double Exp = ExpTable[static_cast<int>(Product*ExpTablePrecision)+TableSize/2];
          Grad = (Label - 1 + 1 / (1 + Exp)) * Alpha;
        }
        for (int i = 0; i < Dimensions; i++) { 
          Neu1eV[i] += Grad * SynNeg(Target,i);
          SynNeg(Target,i) += Grad * SynPos(CurrWord,i);
        }
      }
      for (int i = 0; i < Dimensions; i++) {
        SynPos(CurrWord,i) += Neu1eV[i];
      }
    }
    WordCntAll++;
  }
  }
}

double get_thread_cpu_time() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void LearnEmbeddings(TVVec<TInt, int64>& WalksVV, const int& Dimensions,
  const int& WinSize, const int& Iter, const bool& Verbose,
  TIntFltVH& EmbeddingsHV) {
  TIntIntH RnmH;
  TIntIntH RnmBackH;
  int64 NNodes = 0;

  //renaming nodes into consecutive numbers
  for (int i = 0; i < WalksVV.GetXDim(); i++) {
    for (int64 j = 0; j < WalksVV.GetYDim(); j++) {
      if ( RnmH.IsKey(WalksVV(i, j)) ) {
        WalksVV(i, j) = RnmH.GetDat(WalksVV(i, j));
      } else {
        RnmH.AddDat(WalksVV(i,j),NNodes);
        RnmBackH.AddDat(NNodes,WalksVV(i, j));
        WalksVV(i, j) = NNodes++;
      }
    }
  }

  // Creation of int vector vocab of length Number of nodes
  // It holds (for each node) the number of times it has been visited
  TIntV Vocab(NNodes);
  LearnVocab(WalksVV, Vocab);

  // Create Int vector and Float vector of length number of nodes
  TIntV KTable(NNodes);
  TFltV UTable(NNodes);
  TVVec<TFlt, int64> SynNeg;
  TVVec<TFlt, int64> SynPos;
  //TRnd Rnd(time(NULL));
  TRnd Rnd(1);

  // Initializations
  InitPosEmb(Vocab, Dimensions, Rnd, SynPos);
  InitNegEmb(Vocab, Dimensions, SynNeg);
  InitUnigramTable(Vocab, KTable, UTable);
  TFltV ExpTable(TableSize);

  // From section 3.2.2 of node2vec's paper
  double Alpha = StartAlpha;                              //learning rate
  // Get values into ExpTable (vector of floats)

#pragma omp parallel for schedule(dynamic)
  for (int i = 0; i < TableSize; i++ ) {
    double Value = -MaxExp + static_cast<double>(i) / static_cast<double>(ExpTablePrecision);
    ExpTable[i] = TMath::Power(TMath::E, Value);
  }

  int64 WordCntAll = 0;

    clock_t begin = clock();
    double begin_nat = omp_get_wtime();
// op RS 2016/09/26, collapse does not compile on Mac OS X
//#pragma omp parallel for schedule(dynamic) collapse(2)
  for (int j = 0; j < Iter; j++) {
int size = WalksVV.GetXDim();

// Without this directive, code is fully deterministic
#pragma omp parallel for schedule(dynamic) ordered
    for (int64 i = 0; i < WalksVV.GetXDim(); i++) {
      // For each walk train model
      // This changes SynNeg and SynPos
      
      
      TrainModel(WalksVV, Dimensions, WinSize, Iter, Verbose, KTable, UTable,
       WordCntAll, ExpTable, Alpha, i, Rnd, SynNeg, SynPos);
      
    }
  }
  

  clock_t end = clock();
  double end_nat = omp_get_wtime();
  double _time = double(end - begin)/CLOCKS_PER_SEC;
  double _time_nat = end_nat - begin_nat;
  printf("<train_model process=\"%f\" natural=\"%f\" />", _time, _time_nat);

  if (Verbose) { printf("\n"); fflush(stdout); }

  // Write embeddings in the vector
  for (int64 i = 0; i < SynPos.GetXDim(); i++) {
    TFltV CurrV(SynPos.GetYDim());
    for (int j = 0; j < SynPos.GetYDim(); j++) { CurrV[j] = SynPos(i, j); }
    EmbeddingsHV.AddDat(RnmBackH.GetDat(i), CurrV);
  }
}
