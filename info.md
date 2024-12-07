# Info recabada

## glib-core/dt.h

Parece que este fichero está dedicado principalmente a la definición de tipos de dato básicos


# Info sobre la compilación

Podemos eliminar snap-exp, tutorials, los ejemplos que no sean node2vec, las funciones de snap-adv que no sean n2v o sus dos dependencias ...
Hay bastantes ficheros que pueden ser eliminados sin ningún tipo de problema

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

Después de modificar esto podemos finalemente compilar.

Ahora podemos comenzar a borrar todos los ficheros que no deseemos que salen en Snap y todo va.



Ahora estoy teniendo problemas con los tipos de glib-core que importa bd (que no se si tienen que ser todos o se pueden eliminar algunos)


En glib core se podrían eliminar unos cuantos archivos. No obstante simplemente he comentado sus includes en base.h y base.cpp para evitar que se compilen. He modificado ->
  - json
  - ssmp
  - ss
  - html
  - http
  - url
  - blobbs
  - wch

Supongo que estos no son necesarios porque todo aquello que se utiliza para compilar node2vec no las utiliza (?) pero entonces me extraña en cierta medida que sí que se esté utilizando alguna cosa como xml (?)
