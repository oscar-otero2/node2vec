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
