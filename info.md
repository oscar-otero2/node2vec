# Info recabada

## glib-core/dt.h

Parece que este fichero está dedicado principalmente a la definición de tipos de dato básicos


# Info sobre la compilación

Se utilizan Makefile para compilar.
La base está en snap/

Esta compila los diferentes directorios (el que nos interesa es test, podemos dejar este únicamente)
Dentro de /snap/examples/ tenemos otro makefile al que podemos intruír que únicamente cree el node2vec

Hay otro más sin embargo que se llama Makefile.examin ->
Este makefile es el importante!

Aquí dentro se compila el paquete completo Snap para poderlo integrar con los algoritmos (como node2vec)

Si puedo modificar la compilación de este Snap ya está


Para evitar compilar el test (que requiere de un montón de ficheros que no vienen al caso) podemos modificar el makefile en
/snap-core/Makefile que lo incluye en el all
Para modificar esto, si simplemente eliminamos el all explota porque lo necesita para algo cerca del final de la compilación y revienta