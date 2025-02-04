# Apuntes de dependencias n2v.cpp

- n2v.h
- n2v.cpp

- stdafx.h :
  - targetver.h *(es solo un define)*
  - Snap.h : *#define _USE_MATH_DEFINES    // to use cmath's constants for VS*
    - ## SNAP library :
      - base.h
      - gnuplot.h
      - linalg.h
      - gbase.h
      - util.h
      - attr.h
    - ## graph data structures :
      - graph.h
      - graphmp.h
      - network.h
      - networkmp.h
      - bignet.h
      - timenet.h
      - mmnet.h
    - ## table data structures and algos :
      - table.h
      - conv.h
      - numpy.h
    - ## algos :
      - subgraph.h
      - anf.h
      - bfsdfs.h
      - cncom.h
      - kcore.h
      - alg.h
      - triad.h
      - gsvd.h
      - gstat.h
      - centr.h
      - cmty.h
      - flow.h
      - coreper.h
      - randwalk.h
      - casc.h
      - sim.h
    - ## graph generators :
      - ggen.h
      - ff.h
      - gio.h
      - gviz.h
      - ghash.h
      - statplot.h
      
# Funcións de node2vec

- Clases :
  - PWNet
  - TVVec<TInt, int64>
  - TIntFltVH
  - TIntV
  - TRnd
- Funciones :
  - PreprocessTransitionProbs
  - fflush (esta es del propio cpp)
  - SimulateWalk
  - LearnEmbeddings
- Namespaces ? Creo que son clases :
  - TWNet
    - TNodeI
  - TNGraph
    - TEdgeI
  - TNEANet
    - TEdgeI
    
El TNGraph de aquí realmente es una clase que está definida en snap-core/graph.h/cpp no tiene ninguna dependencia externa

El TNEANet -> snap-core/network.h/cpp
Este fichero no tiene ninguna dep externa
    
<details>
<summary>

PWNet -> snap-adv/biasedrandomwalk.h
  ```cpp
  typedef TPt<TWNet> PWNet
  ```

</summary>


TWNet -> snap-adv/biasedrandomwalk.h
```cpp
typedef TNodeEDatNet<TIntIntVFltVPrH, TFlt> TWNet;
```

TNodeEDatNet<> -> snap-core/network.h 
(Node Edge Network (directed graph, TNGraph with data on nodes and edges))
```text
  Esta es una clase de 200 líneas con muchas dependencias del resto de clases del fichero por lo que lo más fácil será contarlo como un completo.
  Supongo que esto malo será, parece bastante stand alone porque el network.cpp tampoco tiene ningún import de absolutamente nada.
```
  
TIntIntVFltVPrH -> glib-core/hash.h
```cpp
  typedef THash<TInt, TIntVFltVPr> TIntIntVFltVPrH
  
  //Solamente aparece TRES VECES en toda la codebase
```

THash<> -> glib-core/hash.h
(Tabla de hash)
```text
  Esto vuelve a ser una clase grande com muchas dependencias, mejor tener en cuenta el fichero completo.
  Tiene dep de bd.h Este fichero solo son un montón de macros c:
```

TInt -> glib-core/dt.h
(Ints con funciones chulas)
```text
  Vuelve a ser una clase con muchas dependencias
  Solamente depende de bd.h de nuevo, así que podemos simplemente aceptar el fichero.
  El cpp tampoco tiene imports
```
  

TIntVFltVPr -> glib-core/ds.h
```cpp
  typedef TPair<TVec<TInt, int>, TVec<TFlt, int> > TIntVFltVPr
  //Ya resueltos TInt y TFlt
  //No hace falta mirar más
  //ds.h es un fichero sin imports 
  //Incluírlo entero y ya
```
    

TFlt -> glib-core/dt.h
```text
  Tipo float
```


TPt<> -> glib-core/bd.h
  ```cpp
  template <class TRec> class TPt;
  // Smart-Pointer-With-Reference-Count
  template <class TRec>
  class TPt{
  public:
    typedef TRec TObj;
  private:
    TRec* Addr;
    void MkRef() const {
      if (Addr!=NULL){
        Addr->CRef.MkRef();
      }   
    }
    void UnRef() const {
      if (Addr!=NULL){
        Addr->CRef.UnRef();
        if (Addr->CRef.NoRef()){delete Addr;}
      }   
    }
  public:
    TPt(): Addr(NULL){}
    TPt(const TPt& Pt): Addr(Pt.Addr){MkRef();}
    TPt(TRec* _Addr): Addr(_Addr){MkRef();}
    static TPt New(){return TObj::New();}
    ~TPt(){UnRef();}
    explicit TPt(TSIn& SIn);
    explicit TPt(TSIn& SIn, void* ThisPt);
    void Save(TSOut& SOut) const;
    void LoadXml(const TPt<TXmlTok>& XmlTok, const TStr& Nm);
    void SaveXml(TSOut& SOut, const TStr& Nm) const;
  
    TPt& operator=(const TPt& Pt){
      if (this!=&Pt){Pt.MkRef(); UnRef(); Addr=Pt.Addr;} return *this;}
    bool operator==(const TPt& Pt) const {return *Addr==*Pt.Addr;}
    bool operator!=(const TPt& Pt) const {return *Addr!=*Pt.Addr;}
    bool operator<(const TPt& Pt) const {return *Addr<*Pt.Addr;}
  
    TRec* operator->() const {Assert(Addr!=NULL); return Addr;}
    TRec& operator*() const {Assert(Addr!=NULL); return *Addr;}
    TRec& operator[](const int& RecN) const {
      Assert(Addr!=NULL); return Addr[RecN];}
    TRec* operator()() const {return Addr;}
    //const TRec* operator()() const {return Addr;}
    //TRec* operator()() {return Addr;}
  
    bool Empty() const { return Addr==NULL;}
    void Clr(){UnRef(); Addr=NULL;}
    int GetRefs() const {
      if (Addr==NULL){return -1;} else {return Addr->CRef.GetRefs();}}
  
    int GetPrimHashCd() const {return Addr->GetPrimHashCd();}
    int GetSecHashCd() const {return Addr->GetSecHashCd();}
  
    TPt<TRec> Clone(){return MkClone(*this);}
  };
  ```

</details>


TVVec -> glib-core/ds.h
```text
  template <class TVal, class TSizeTy = int>
  Es básicamente una clase que define vectores bidimensionales.
  Solamente se define de nuevo en ds.h
```

TIntFltVH -> glib-core/hash.h
```text
  typedef THash<TInt, TFltV> TIntFltVH;
  En este caso vuelve a ser simplemente un typedef en hash.h así que no nos requerirá nada a mayores.
  Es un hashmap de TInt (ints) y TFltV (typedef TVec<TFlt> que es (float))
  La clase viene a ser Tipo Int y FloatVector Hashmap
```

TIntV -> glib-core/ds.h
```text
  typedef TVec<TInt>
  Es un vector de TInt
```

TRnd -> glib-core/dt.h
```text
  Es una clase para representar números aleatorios
  De nuevo completamente definida en dt.h
```

## Functions

PreprocessTransitionProbs -> snap-adv/biasedrandomwalk.h/cpp
```text
  Aquí en el .h no hay ninguna dependencia rara (es donde se definen TWNet y PWNet)

  Dependencia con TIntIntVFltPrH()
  Por alguna razón esto es un typedef en hash.h
  Debe ser un constructor por defecto.
  Creo que con nada más que no sean tipos de datos de dt o ds
```

SimulateWalk -> snap-adv/biasedrandomwalk.h/cpp
AliasDrawInt()
Está definido en el mismo fichero ->
```cpp
  int64 AliasDrawInt(TIntVFltVPr& NTTable, TRnd& Rnd) {
    int64 N = NTTable.GetVal1().Len();
    TInt X = static_cast<int64>(Rnd.GetUniDev()*N);
    double Y = Rnd.GetUniDev();
    return Y < NTTable.GetVal2()[X] ? X : NTTable.GetVal1()[X];
  }
```
```text
  Aquí dependemos solamente de tipos de dt.h y ds.h
  Random y par de vector de flotante y vector de int
```


LearnEmbeddings -> snap-adv/word2vec.h/cpp
```text
  TVVec (ds)
  TIntFltVH (dt / ds)
  

  LearnVocab -> En el propio fichero
    No tiene deps adicionales

  InitPosEmb -> En el propio fichero
    No tiene deps adicionales

  InitNegEmb -> En el propio fichero
    No tiene deps adicionales

  InitUnigramTable -> En el propio fichero
    No tiene deps adicionales
    
  TrainModel -> En el propio fichero
    RndUnigramInt() -> En el propio fichero
      No tiene deps adicionales

    SynPos() -> Creo no no es función, es un TVVec<TFlt, int64>
    SynNeg() -> Creo no no es función, es un TVVec<TFlt, int64>

  TMath(librería estándar de mate?) -> glib-core/xmath.h
    Este archivo depende de bd.h (como dt y ds)
    Necesitamos evidentemente el cpp también
```

Todas las clases que requieren estas funciones en principio deberían de estar ya mencionadas(?)
Revisar aún así una por una.


# Resumen

```cpp
void node2vec(PWNet& InNet, const double& ParamP, const double& ParamQ,
  const int& Dimensions, const int& WalkLen, const int& NumWalks,
  const int& WinSize, const int& Iter, const bool& Verbose,
  const bool& OutputWalks, TVVec<TInt, int64>& WalksVV,
  TIntFltVH& EmbeddingsHV) {
  //Preprocess transition probabilities
  PreprocessTransitionProbs(InNet, ParamP, ParamQ, Verbose);
  TIntV NIdsV;
  for (TWNet::TNodeI NI = InNet->BegNI(); NI < InNet->EndNI(); NI++) {
    NIdsV.Add(NI.GetId());
  }
  //Generate random walks
  int64 AllWalks = (int64)NumWalks * NIdsV.Len();
  WalksVV = TVVec<TInt, int64>(AllWalks,WalkLen);
  TRnd Rnd(time(NULL));
  int64 WalksDone = 0;
  for (int64 i = 0; i < NumWalks; i++) {
    NIdsV.Shuffle(Rnd);
#pragma omp parallel for schedule(dynamic)
    for (int64 j = 0; j < NIdsV.Len(); j++) {
      if ( Verbose && WalksDone%10000 == 0 ) {
        printf("\rWalking Progress: %.2lf%%",(double)WalksDone*100/(double)AllWalks);fflush(stdout);
      }
      TIntV WalkV;
      SimulateWalk(InNet, NIdsV[j], WalkLen, Rnd, WalkV);
      for (int64 k = 0; k < WalkV.Len(); k++) { 
        WalksVV.PutXY(i*NIdsV.Len()+j, k, WalkV[k]);
      }
      WalksDone++;
    }
  }
  if (Verbose) {
    printf("\n");
    fflush(stdout);
  }
  //Learning embeddings
  if (!OutputWalks) {
    LearnEmbeddings(WalksVV, Dimensions, WinSize, Iter, Verbose, EmbeddingsHV);
  }
}
```

Esta es la función base de node2vec. El resto de funciones que hay definidas en el fichero simplemente ejecutan la función partiendo de un menor número de parámetros. La idea es ver de que dependen cada una de las clases, funciones, y namespaces encontrados y aislarlos.

Este mini análisis sirve finalmente para ver el código que más flagrantemente es dependencia de la función a analizar, no obstante, algunas de las clases identificadas tienen dependencias con otras, por lo que algunos ficheros a mayores han sido identificados.

Nuestra lista de dependencias acaba siendo:
  - stdafx.h
  - base.cpp | base.h :
    - bd.cpp | bd.h
    - fl.cpp | fl.h
    - dt.cpp | dt.h
    - ut.cpp | ut.h
    - ds.h
    - hash.cpp | hash.h
    - shash.h
    - unicode.cpp | unicode.h
    - tm.cpp | tm.h
    - os.cpp | os.h
    - bits.cpp | bits.h
    - env.cpp | env.h
    - xfl.cpp | xfl.h
    - xmath.cpp | xmath.h
    - lx.cpp | lx.h
    - md5.cpp | md5.h
    - xml.cpp | xml.h
  - gbase.cpp | gbase.h
  - attr.cpp | attr.h
  - graph.cpp | graph.h
  - network.cpp | network.h

Con estas dependencias el código compila sin ningún problema.
De modificarse partes del código, probablemente algunas de estas podrían ser eliminadas, pero he decidido intentar tocar lo menos posible el código para que se mantenga lo más íntegro posible.

Finalmente en cuanto a los tests los he desactivado dado que no he encontrado ninguna documentación en cuanto a como utilizarlos y con las dependencias que he supuesto (la librería de gtest) no funcionan ni en el repositorio de snap sin ninguna modificación. Estos tests son unitarios de algunas de las clases, cubren clases que realmente no son relevantes (por lo menos en principio, para la modificación de node2vec).