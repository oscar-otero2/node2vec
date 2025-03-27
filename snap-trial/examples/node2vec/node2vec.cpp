#include "stdafx.h"

#include "n2v.h"
#include <ctime>
#include <unistd.h>

#ifdef USE_OPENMP
#include <omp.h>
#endif

void ParseArgs(int& argc, char* argv[], TStr& InFile, TStr& OutFile,
 int& Dimensions, int& WalkLen, int& NumWalks, int& WinSize, int& Iter,
 bool& Verbose, double& ParamP, double& ParamQ, bool& Directed, bool& Weighted,
 bool& OutputWalks) {
  Env = TEnv(argc, argv, TNotify::StdNotify);
  Env.PrepArgs(TStr::Fmt("\nAn algorithmic framework for representational learning on graphs."));
  InFile = Env.GetIfArgPrefixStr("-i:", "graph/karate.edgelist",
   "Input graph path");
  OutFile = Env.GetIfArgPrefixStr("-o:", "emb/karate.emb",
   "Output graph path");
  Dimensions = Env.GetIfArgPrefixInt("-d:", 128,
   "Number of dimensions. Default is 128");
  WalkLen = Env.GetIfArgPrefixInt("-l:", 80,
   "Length of walk per source. Default is 80");
  NumWalks = Env.GetIfArgPrefixInt("-r:", 10,
   "Number of walks per source. Default is 10");
  WinSize = Env.GetIfArgPrefixInt("-k:", 10,
   "Context size for optimization. Default is 10");
  Iter = Env.GetIfArgPrefixInt("-e:", 1,
   "Number of epochs in SGD. Default is 1");
  ParamP = Env.GetIfArgPrefixFlt("-p:", 1,
   "Return hyperparameter. Default is 1");
  ParamQ = Env.GetIfArgPrefixFlt("-q:", 1,
   "Inout hyperparameter. Default is 1");
  Verbose = Env.IsArgStr("-v", "Verbose output.");
  Directed = Env.IsArgStr("-dr", "Graph is directed.");
  Weighted = Env.IsArgStr("-w", "Graph is weighted.");
  OutputWalks = Env.IsArgStr("-ow", "Output random walks instead of embeddings.");
}

// Updates the PWNet InNet according to the actual graph we input
void ReadGraph(TStr& InFile, bool& Directed, bool& Weighted, bool& Verbose, PWNet& InNet) {
  TFIn FIn(InFile);
  int64 LineCnt = 0;
  try {
    // Parse each line in graph
    while (!FIn.Eof()) {
      TStr Ln;
      FIn.GetNextLn(Ln);
      TStr Line, Comment;
      
      // Comments specified as #
      // Also split with spaces into tokens
      Ln.SplitOnCh(Line,'#',Comment);
      TStrV Tokens;
      Line.SplitOnWs(Tokens);
      
      // If only one token (no meaning) or empty line, don't do anything
      if(Tokens.Len()<2){ continue; }

      // Get the two (2) ints (that supposedly represent nodes of graph)
      int64 SrcNId = Tokens[0].GetInt();
      int64 DstNId = Tokens[1].GetInt();
      double Weight = 1.0;
      
      // If the graph is weighted the third token will be weight
      if (Weighted) { Weight = Tokens[2].GetFlt(); }
      
      // InNet is a PWNet that has just been created in main 
      // This class represents a pointer to a graph that has
      // (Some value) for nodes and tFlt for edges 
      
      // Creates the specified nodes if they don't already exist
      if (!InNet->IsNode(SrcNId)){ InNet->AddNode(SrcNId); }
      if (!InNet->IsNode(DstNId)){ InNet->AddNode(DstNId); }
      
      // We add the described edge with the desired weight
      // 1.0 if not weighted
      InNet->AddEdge(SrcNId,DstNId,Weight);
      
      // If our graph is not directed we also add the opposite direction
      // As a new edge
      if (!Directed){ InNet->AddEdge(DstNId,SrcNId,Weight); }
      
      // Count number of read lines
      LineCnt++;
    }
    
    //Self explanatory
    if (Verbose) { printf("Read %lld lines from %s\n", (long long)LineCnt, InFile.CStr()); }

  } catch (PExcept Except) {
    if (Verbose) {
      printf("Read %lld lines from %s, then %s\n", (long long)LineCnt, InFile.CStr(),
       Except->GetStr().CStr());
    }
  }
}

// Write desired Output
void WriteOutput(TStr& OutFile, TIntFltVH& EmbeddingsHV, TVVec<TInt, int64>& WalksVV,
 bool& OutputWalks) {
  
  // If OutputWalks was specified during execution then
  // Print all full walks (-r walks from each node with 
  // length -l)
  TFOut FOut(OutFile);
  if (OutputWalks) {
    for (int64 i = 0; i < WalksVV.GetXDim(); i++) {
      for (int64 j = 0; j < WalksVV.GetYDim(); j++) {
        // For every int in WalksVV (vector vector [][]-like structure)
        // Write an int for its value (group while Y Dim)
        // Sepparation for YDim is ' ', XDim is '\n'
        FOut.PutInt(WalksVV(i,j));
	      if(j+1==WalksVV.GetYDim()) {
          FOut.PutLn();
	      } else {
          FOut.PutCh(' ');
	      }
      }
    }
    return;
  }
  
  // Only if NOT OutputWalks
  bool First = 1;
  
  // For all embeddings Do something(?)
  // Write this time Embeddings instead of walks
  // From this Hashmap Vector (?)
  for (int i = EmbeddingsHV.FFirstKeyId(); EmbeddingsHV.FNextKeyId(i);) {
    if (First) {
      // Amount of nodes in graph
      FOut.PutInt(EmbeddingsHV.Len());
      FOut.PutCh(' ');
      
      // Amount of dimensions for each node
      // This is -d (number of dimensions) parameter
      FOut.PutInt(EmbeddingsHV[i].Len());
      FOut.PutLn();
      First = 0;
    }
    
    // Prints node number
    FOut.PutInt(EmbeddingsHV.GetKey(i));

    for (int64 j = 0; j < EmbeddingsHV[i].Len(); j++) {
      FOut.PutCh(' ');
      
      // Prints full embeddings (all dimensions) for each node
      FOut.PutFlt(EmbeddingsHV[i][j]);
    }
    FOut.PutLn();
  }
}

int main(int argc, char* argv[]) {
  TStr InFile,OutFile;
  int Dimensions, WalkLen, NumWalks, WinSize, Iter;
  double ParamP, ParamQ;
  bool Directed, Weighted, Verbose, OutputWalks;
  

  // File to write my custom output. (first arg)
  // Super mega prone to error
  // I need to make this creat and/or append
  char* str = argv[1];
  int outfile_fd = open(str, O_WRONLY | O_CREAT | O_APPEND, 0664);
  FILE* outfile = fdopen(outfile_fd, "w");
  
  close(STDOUT_FILENO);

  // Just parses arguments to allow for execution

  // This is measuring time for all cores
  clock_t begin = clock();
  double begin_nat = omp_get_wtime();

  ParseArgs(argc, argv, InFile, OutFile, Dimensions, WalkLen, NumWalks, WinSize,
   Iter, Verbose, ParamP, ParamQ, Directed, Weighted, OutputWalks);

  // Empty all ParseArgs exit things
  fflush(stdout);
  dup2(outfile_fd, STDOUT_FILENO);

  clock_t end = clock();
  double end_nat = omp_get_wtime();
  double time = double(end-begin) / CLOCKS_PER_SEC;
  double time_nat = end_nat - begin_nat;
  // printf("Time to parse %f\n", time);
  // printf("Time to parse NAT %f\n", time_nat);
  printf("<execution threads=\"%d\">", omp_get_max_threads());
  printf("<parse process=\"%f\" natural=\"%f\" />", time, time_nat);

  // We create our super new graph
  PWNet InNet = PWNet::New();
  
  // This is a Float Vector Hashmap, so a hashmap of all Embedding vectors I think
  TIntFltVH EmbeddingsHV;
  TVVec <TInt, int64> WalksVV;
  
  // Write InNet with our graph file
  
  begin = clock();
  begin_nat = omp_get_wtime();
  
  ReadGraph(InFile, Directed, Weighted, Verbose, InNet);
  
  end = clock();
  end_nat  = omp_get_wtime();
  time = double(end-begin) / CLOCKS_PER_SEC;
  time_nat = end_nat - begin_nat;
  // printf("Time to read graph %f\n", time);
  // printf("Time to read graph NAT %f\n", time_nat);
  printf("<read_graph process=\"%f\" natural=\"%f\" />", time, time_nat);
  
  // Apply node2vec algorithm
  // EmbeddingsHV and WalksVV have just been created
  
  begin = clock();
  begin_nat = omp_get_wtime();
  
  printf("<node2vec>");

  // Here for some reason everything gets nicely redirected
  //close(STDOUT_FILENO);

  node2vec(InNet, ParamP, ParamQ, Dimensions, WalkLen, NumWalks, WinSize, Iter, 
   Verbose, OutputWalks, WalksVV, EmbeddingsHV);

  end = clock();
  end_nat = omp_get_wtime();
  time = double(end-begin) / CLOCKS_PER_SEC;
  time_nat = end_nat - begin_nat;
  // printf("Time to execute node2vec %f\n", time);
  // printf("Time to execute node2vec NAT %f\n", time_nat);
  printf("<process>%f</process>", time);
  printf("<natural>%f</natural>", time_nat);
  printf("</node2vec>", time, time_nat);

  printf("</execution>");
  // Write desired output (either walks or embeddings)
  WriteOutput(OutFile, EmbeddingsHV, WalksVV, OutputWalks);
  
  fclose(outfile);
  return 0;
}
