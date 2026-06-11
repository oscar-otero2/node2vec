# Aislamiento del algoritmo node2vec de la librería SNAP

4 versiones de la paralelización disponibles, cada una en su correspondiente rama 

### Versión 1:
Rama parallel-v1, distribución en la que se reparte un mismo número de nodos para cada uno de los procesos.

### Versión 2:
Rama parallel-v2, distribución en la que se reparte un segmento, con un balanceo basado en la cantidad de aristas, a cada uno de los procesos.

### Versión 3:
Rama parallel-v3, esquema maestro esclavo con una distribución en bloques de cantidad configurable con el switch `-b:<número>` al invocar al ejecutable. Rank 0 no trabaja.

### Versión 4:
Rama parallel-v4, esquema maestro esclavo con una distribución en bloques basado en RMA. Rank 0 trabaja.

---

Para todos estos ejemplos, la compilación se puede llevar a cabo dirigiéndose al directorio snap-trial y ejecutando `make`. Esto compilará la librería (Snap.o) y el ejecutable de node2vec (node2vec), dentro del directorio `snap-trial/examples/node2vec/`. En ese directorio se encuentra, en el Readme, el comando que se puede ejecutar para compilar node2vec sin necesidad de recompilar la librería (Snap.o).

---

Ejemplo de uso

`./node2vec out.xml -i:graph/karate.edgelist -o:emb/karate.emb -l:3 -d:24 -p:0.3 -dr -v`

Al ejecutar la versión paralela será necesario utilzar mpirun:

`mpirun -np 2 node2vec out.xml -i:graph/karate.edgelist -o:emb/karate.emb -l:3 -d:24 -p:0.3 -dr -v`

Los parámetros más relevantes son: 
- El fichero justo después del ejecutable, que será a donde escribir la salida de los tiempos, formateada como xml. 
- El fichero precedido por `-i:` será la red de entrada.
- El fichero precedido por `-o:` será el de salida.
- Utilizando el flag `-dr` se especificará que la red es dirigida, si no se escribe, se toman todas las aristas del fichero como bidireccionales.
- El flag `-ow` permite no ejecutar la optimización por SGD y simplemente tomar como salida los caminos aleatorios.

El resto de parámetros utilizables:

Parameters:
Input graph path (-i:)
Output graph path (-o:)
Number of dimensions. Default is 128 (-d:)
Length of walk per source. Default is 80 (-l:)
Number of walks per source. Default is 10 (-r:)
Context size for optimization. Default is 10 (-k:)
Number of epochs in SGD. Default is 1 (-e:)
Return hyperparameter. Default is 1 (-p:)
Inout hyperparameter. Default is 1 (-q:)
Verbose output. (-v)
Graph is directed. (-dr)
Graph is weighted. (-w)
Output random walks instead of embeddings. (-ow)


---

Nota importante -> Por la realización de las pruebas, la fase de caminado está comentada, en el fichero `/snap-trial/snap-adv/n2v.cpp`. Esto provoca que, sin descomentarla, los resultados no sean correctos.
